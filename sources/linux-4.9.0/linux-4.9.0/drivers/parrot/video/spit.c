/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/kobject.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/kfifo.h>
#include <linux/dma-direction.h>
#include <asm/cacheflush.h>
#include <linux/spit.h>
#include <linux/dma-mapping.h>

#define MODULE_NAME "spit"
#define DEFAULT_FRAME_FIFO_DEPTH 128

#define SPIT_WAIT_TIMEOUT (5*HZ)

enum spit_ioctl_copy {
	SPIT_IOCTL_COPY_NONE = 0,
	SPIT_IOCTL_COPY_FROM_USER = 1,
	SPIT_IOCTL_COPY_TO_USER = 2,
	SPIT_IOCTL_COPY_BOTH = 3
};

struct spit_fifo_map {
	/* Latest FIFO info from ThreadX */
	struct spit_buffer_fifo_info	 info;
	/* Current mapped memory (can be different from @info) */
	void				*paddr;
	off_t				 offset;
	void				*vaddr;
	size_t				 size;
	atomic_t			 cache_enabled;
};

struct spit_file {
	struct spit_devdata		*spit_devdata;
	/* Frame types mapped */
	enum spit_frame_types		 frame_types_mapped;
	/* Frame FIFO */
	unsigned int			 frame_fifo_depth;
	DECLARE_KFIFO_PTR(frame_fifo, struct spit_frame_desc);
};

struct spit_devdata {
	struct device			*dev;
	struct mutex			 lock;
	u8				 stream;
	/* ThreadX rpmsg sync
	 * Two wait queues are used to synchronize rpmsg with IOCTLs:
	 *  - data_wait: for GET_FRAME synchronization (high frequency call)
	 *  - ctrl_wait: for all other IOCTLs synchronization
	 * To manage which rpmsg has been received, a bitmask named
	 * wait_flag is defined and used for condition in all wait_event_ calls.
	 */
	wait_queue_head_t		 ctrl_wait;
	wait_queue_head_t		 data_wait;
	unsigned int			 wait_flags;
	struct spit_rpmsg_stream resp[SPIT_RPMSG_STREAM_TYPE_RESP_COUNT];
	struct mutex ctrl_lock[SPIT_RPMSG_STREAM_TYPE_RESP_COUNT];

	/* Stream states */
	int				 initialized;
	enum spit_frame_types		 started;
	/* Frame FIFO buffer */
	struct spit_fifo_map		 fifo_map[SPIT_FRAME_TYPE_COUNT*2];
	struct spit_input_status	 input_status;
	atomic_t			 input_status_updated;
	/* File entry */
	struct miscdevice		 misc_dev;
	char				 devname[32];
	atomic_t			 opened;
	struct spit_file		*file_ref[SPIT_FRAME_TYPE_COUNT * 2];
};

static struct spit_devdata *spit_devs;
static struct rpmsg_device *rp_device;

static const char *spit_error_to_string(enum spit_error err)
{
	static DECLARE_SPIT_ERROR_STRING(error_str);
	int i;

	for (i = 0; i < ARRAY_SIZE(error_str); i++)
		if (error_str[i].err == err)
			return error_str[i].str;

	return "unknown error";
}

static struct spit_devdata *spit_get_devdata(int stream)
{
	if (stream < 0 || stream >= SPIT_STREAM_COUNT)
		return NULL;

	return &spit_devs[stream];
}

static struct spit_devdata *spit_get_devdata_by_minor(int minor)
{
	int i;

	for (i = 0; i < SPIT_STREAM_COUNT; i++) {
		if (spit_devs[i].misc_dev.minor == minor)
			return &spit_devs[i];
	}

	return NULL;
}

static int spit_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			 void *priv, u32 src)
{
	struct spit_devdata *spit_devdata;
	struct spit_rpmsg_stream *msg = data;
	struct spit_file *spit_file;
	struct spit_frame_desc first_frame, *last_frame;
	unsigned char *first_addr, *last_addr;
	unsigned long flag;
	int idx;

	if (len != sizeof(struct spit_rpmsg_stream)) {
		dev_err(&rpdev->dev, "received message with invalid length\n");
		return -EINVAL;
	}

	/* Get spit data device data */
	spit_devdata = spit_get_devdata(msg->stream);
	if (!spit_devdata)
		return -ENODEV;

	if (msg->type == SPIT_RPMSG_STREAM_TYPE_EVT_FRAME) {
		/* Do not accept buffer if device is closed or stream is not
		 * started for specific frame type.
		 */
		if (atomic_read(&spit_devdata->opened) == 0 ||
		    !SPIT_FRAME_TYPES_HAS_TYPE(spit_devdata->started,
					       msg->frame_desc.type))
			return -ENOENT;

		/* Lock access to frame FIFO */
		mutex_lock(&spit_devdata->lock);

		/* Get file instance */
		spit_file = spit_devdata->file_ref[msg->frame_desc.type];
		if (!spit_file ||
		    !SPIT_FRAME_TYPES_HAS_TYPE(spit_file->frame_types_mapped,
					       msg->frame_desc.type)) {
			mutex_unlock(&spit_devdata->lock);
			return -ENOENT;
		}

		/* Get frame descriptors */
		last_frame = &msg->frame_desc;
		while (kfifo_peek(&spit_file->frame_fifo, &first_frame)) {
			/* Get frame data addresses */
			first_addr = spit_get_frame_data(&first_frame);
			last_addr = spit_get_frame_data(last_frame);

			/* Check frame validity */
			/* TODO: handle the circular span case */
			if (first_addr + first_frame.size <= last_addr ||
			    last_addr + last_frame->size <= first_addr)
				break;

			/* Frame is invalid (last frame is overlaping oldest */
			dev_warn(spit_devdata->dev, "oldest frame is invalid: drop %u\n",
				 first_frame.seqnum);
			kfifo_skip(&spit_file->frame_fifo);
		}

		/* Add the buffer to the frame FIFO */
		if (kfifo_is_full(&spit_file->frame_fifo)) {
			dev_warn(spit_devdata->dev, "fifo is full, drop oldest frame\n");
			kfifo_skip(&spit_file->frame_fifo);
		}
		kfifo_put(&spit_file->frame_fifo, *last_frame);

		/* Unlock access to frame FIFO */
		mutex_unlock(&spit_devdata->lock);

		/* Inform of data available */
		wake_up(&spit_devdata->data_wait);

		dev_dbg(&rpdev->dev, "received buffer message stream:%u seqnum:%lu datapt:0x%p size:%u\n",
			msg->stream, (unsigned long) msg->frame_desc.seqnum,
			spit_get_frame_data(&msg->frame_desc),
			msg->frame_desc.size);
	} else if (msg->type == SPIT_RPMSG_STREAM_TYPE_EVT_INPUT_STATUS) {
		/* Lock access to input status */
		mutex_lock(&spit_devdata->lock);

		/* Copy input status */
		spit_devdata->input_status = msg->input_status;

		/* Unlock access to input status */
		mutex_unlock(&spit_devdata->lock);

		/* Inform of new input status available */
		atomic_set(&spit_devdata->input_status_updated, 1);
		wake_up(&spit_devdata->data_wait);
	} else if (SPIT_RPMSG_STREAM_IS_RESP(msg->type)) {
		/* Get response index (from 0 to N) */
		idx = SPIT_RPMSG_STREAM_RESP_IDX(msg->type);
		flag = (1 << idx);

		/* Copy response and clear wait flag */
		mutex_lock(&spit_devdata->lock);
		spit_devdata->resp[idx] = *msg;
		spit_devdata->wait_flags &= ~flag;
		mutex_unlock(&spit_devdata->lock);

		/* Wake up IOCTL */
		wake_up(&spit_devdata->ctrl_wait);
	} else {
		dev_warn(&rpdev->dev, "Received unknown message");
	}

	return 0;
}

static int spit_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void spit_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id spit_rpmsg_id_table[] = {
	{ .name	= SPIT_RPMSG_STREAM_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, spit_rpmsg_id_table);

static struct rpmsg_driver spit_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= spit_rpmsg_id_table,
	.probe		= spit_rpmsg_probe,
	.callback	= spit_rpmsg_cb,
	.remove		= spit_rpmsg_remove,
};

static int spit_open(struct inode *inode, struct file *filp)
{
	struct spit_devdata *spit_devdata;
	struct spit_file *spit_file;
	int minor;

	minor = iminor(inode);

	spit_devdata = spit_get_devdata_by_minor(minor);
	if (!spit_devdata)
		return -ENOENT;

	/* Allocate spit file instance */
	spit_file = kzalloc(sizeof(*spit_file), GFP_KERNEL);
	if (!spit_file)
		return -ENOMEM;

	/* Initialize file instance */
	INIT_KFIFO(spit_file->frame_fifo);
	spit_file->frame_fifo_depth = DEFAULT_FRAME_FIFO_DEPTH;
	spit_file->spit_devdata = spit_devdata;

	filp->private_data = (void *) spit_file;
	atomic_inc(&spit_devdata->opened);

	return 0;
}

static int spit_close(struct inode *inode, struct file *filp)
{
	struct spit_file *spit_file = filp->private_data;
	struct spit_devdata *spit_devdata = spit_file->spit_devdata;
	struct spit_rpmsg_stream rpmsg;

	/* Free file instance */
	kfree(spit_file);

	if (!atomic_dec_and_test(&spit_devdata->opened))
		return 0;

	mutex_lock(&spit_devdata->lock);
	if (spit_devdata->started) {
		/* Stop all current frames stream on device */
		dev_warn(spit_devdata->dev, "stream not stopped: force stream stopping\n");
		rpmsg.type = SPIT_RPMSG_STREAM_TYPE_REQ_STOP;
		rpmsg.frame_types = spit_devdata->started;
		rpmsg.stream = spit_devdata->stream;
		rpmsg_send(rp_device->ept, &rpmsg, sizeof(rpmsg));
	}
	spit_devdata->initialized = 0;
	spit_devdata->started = 0;
	mutex_unlock(&spit_devdata->lock);

	return 0;
}

static void spit_flush_buffers(struct spit_devdata *spit_devdata)
{
	int i;

	for (i = 0; i < SPIT_FRAME_TYPE_COUNT; i++) {
		struct spit_file *spit_file = spit_devdata->file_ref[i];

		if (!spit_file)
			continue;

		dev_info(spit_devdata->dev, "%d old buffers flushed\n",
			 kfifo_len(&spit_file->frame_fifo));
		kfifo_reset(&spit_file->frame_fifo);
	}
}

static int spit_send_request(struct spit_devdata *spit_devdata, void *data,
			     size_t size, enum spit_ioctl_copy copy,
			     enum spit_rpmsg_stream_type type,
			     struct spit_rpmsg_stream **resp)
{
	struct spit_rpmsg_stream rpmsg;
	int idx = SPIT_RPMSG_STREAM_REQ_IDX(type);
	unsigned long flag = (1 << idx);
	int ret;

	/* Setup RPMSG */
	rpmsg.stream = spit_devdata->stream;
	rpmsg.type = type;

	/* Copy data */
	if (data) {
		if (copy & SPIT_IOCTL_COPY_FROM_USER) {
			if (copy_from_user(&rpmsg.data, (void __user *) data,
					   size))
				return -EFAULT;
		} else
			memcpy(&rpmsg.data, data, size);
	}

	mutex_lock(&spit_devdata->ctrl_lock[idx]);

	/* Setup wait flag */
	mutex_lock(&spit_devdata->lock);
	spit_devdata->wait_flags |= flag;
	mutex_unlock(&spit_devdata->lock);

	/* Send request to ThreadX */
	rpmsg_send(rp_device->ept, &rpmsg, sizeof(rpmsg));

	/* Wait for response from ThreadX */
	ret = wait_event_interruptible_timeout(spit_devdata->ctrl_wait,
					     !(spit_devdata->wait_flags & flag),
					     SPIT_WAIT_TIMEOUT);

	/* Timeout or error */
	if (ret <= 0) {
		/* Clear waiting flag */
		mutex_lock(&spit_devdata->lock);
		spit_devdata->wait_flags &= ~flag;
		mutex_unlock(&spit_devdata->lock);
		dev_err(spit_devdata->dev, "request 0x%04x failed: %s\n", type,
			ret ? "rpmsg channel error" : "timeout");

		mutex_unlock(&spit_devdata->ctrl_lock[idx]);
		return ret == 0 ? -ETIMEDOUT : -ERESTARTSYS;
	}

	/* Check error status */
	ret = spit_devdata->resp[idx].error;
	if (ret != SPIT_ERROR_NONE) {
		dev_err(spit_devdata->dev, "request 0x%04x failed: %s\n", type,
			spit_error_to_string(ret));
		mutex_unlock(&spit_devdata->ctrl_lock[idx]);
		return ret > 0 ? -ret : ret;
	}

	/* Set response */
	if (resp)
		*resp = &spit_devdata->resp[idx];

	/* Copy to user */
	if (data && copy & SPIT_IOCTL_COPY_TO_USER) {
		mutex_lock(&spit_devdata->lock);
		if (copy_to_user((void __user *) data,
		    spit_devdata->resp[idx].data, size)) {
			mutex_unlock(&spit_devdata->lock);
			mutex_unlock(&spit_devdata->ctrl_lock[idx]);
			return -EFAULT;
		}
		mutex_unlock(&spit_devdata->lock);
	}

	mutex_unlock(&spit_devdata->ctrl_lock[idx]);

	return 0;
}

static int spit_request_fifo(struct spit_devdata *spit_devdata, int input,
			     void *data)
{
	struct spit_buffer_fifo_info info;
	struct spit_rpmsg_stream *resp;
	struct spit_fifo fifo;
	unsigned long paddr;
	size_t size;
	int ret;

	/* Copy frame type to request */
	if (copy_from_user(&info.type, (void __user *) data +
			   offsetof(struct spit_fifo, type),
			   sizeof(info.type)))
		return -EFAULT;

	/* Check frame type */
	if (info.type >= SPIT_FRAME_TYPE_COUNT)
		return -EINVAL;

	/* Request for input buffer FIFO info */
	info.flags = input ? SPIT_BUFFER_FIFO_FLAGS_INPUT : 0;

	/* Send request and wait FIFO info */
	ret = spit_send_request(spit_devdata, &info, sizeof(info),
				SPIT_IOCTL_COPY_NONE,
				SPIT_RPMSG_STREAM_TYPE_REQ_GET_FIFO_INFO,
				&resp);
	if (ret)
		return ret;

	/* FIFO which have truncated frame must be page aligned */
	paddr = (unsigned long) spit_get_buffer_fifo_phys(&resp->fifo_info);
	size = resp->fifo_info.buf_size;
	if (resp->fifo_info.flags & SPIT_BUFFER_FIFO_FLAGS_TRUNC) {
		if ((paddr & PAGE_MASK) != paddr ||
		    (size & PAGE_MASK) != size) {
			dev_err(spit_devdata->dev,
			     "FIFO with truncated frames must be page aligned");
			return -ENOMEM;
		}
	} else {
		/* Align FIFO buffer address and size */
		size = round_up(size + (paddr & ~PAGE_MASK), PAGE_SIZE);
		paddr &= PAGE_MASK;
		spit_set_buffer_fifo_phys(&resp->fifo_info,
					  (unsigned char *) paddr);
		resp->fifo_info.buf_size = size;
	}

	/* Set input FIFO map in second part of FIFO map table */
	fifo.type = info.type;
	if (resp->fifo_info.flags & SPIT_BUFFER_FIFO_FLAGS_INPUT)
		info.type += SPIT_FRAME_TYPE_COUNT;

	/* Store the FIFO info */
	mutex_lock(&spit_devdata->lock);
	spit_devdata->fifo_map[info.type].info = resp->fifo_info;
	mutex_unlock(&spit_devdata->lock);

	/* Set FIFO info for this frame type */
	fifo.offset = info.type * PAGE_SIZE;
	fifo.length = size;

	/* Set buffer twice to force a double mapping in order to avoid
	 * truncated frames (not supported for input FIFO buffer)
	 */
	if (resp->fifo_info.flags & SPIT_BUFFER_FIFO_FLAGS_TRUNC &&
	    !(resp->fifo_info.flags & SPIT_BUFFER_FIFO_FLAGS_INPUT))
		fifo.length *= 2;

	/* Copy result to user */
	if (copy_to_user((void __user *) data, &fifo, sizeof(fifo)))
		return -EFAULT;

	return 0;
}

static inline void spit_invalidate_cache(struct spit_devdata *spit_devdata,
					 enum spit_frame_type type, void *paddr,
					 size_t size)
{
	struct spit_fifo_map *map = &spit_devdata->fifo_map[type];
	long overflow;

	/* No need to invalidate frame */
	if (!atomic_read(&map->cache_enabled) ||
	    map->info.flags & SPIT_BUFFER_FIFO_FLAGS_CACHED)
		return;

	/* Calculate overflow for truncated frames */
	overflow = (paddr + size) - (map->paddr + map->size);
	overflow = (overflow < 0) ? 0 : overflow;

	/* Invalidate buffer area L1 and L2 caches */
	dma_sync_single_for_cpu(spit_devdata->dev, (dma_addr_t) paddr,
				size - overflow, DMA_FROM_DEVICE);

	/* Invalidate tail of frame in head buffer */
	if (overflow) {
		if (map->info.flags & SPIT_BUFFER_FIFO_FLAGS_TRUNC)
			dma_sync_single_for_cpu(spit_devdata->dev,
						(dma_addr_t) map->paddr,
						overflow, DMA_FROM_DEVICE);
		else
			dev_err(spit_devdata->dev,
				"truncated frame detected in a continuous FIFO buffer\n");
	}
}

static long spit_ioctl(struct file *filp, unsigned int req,
		       unsigned long arg)
{
	struct spit_file *spit_file = filp->private_data;
	struct spit_devdata *spit_devdata = spit_file->spit_devdata;
	struct spit_frame_desc frame_desc;
	enum spit_frame_types frame_types;
	enum spit_frame_type frame_type;
	int ret;

	if (!rp_device)
		return -EAGAIN;

	switch (req) {
	case SPIT_GET_CAPABILITIES:
		/* Send request and wait capabilities */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				    sizeof(struct spit_stream_caps),
				    SPIT_IOCTL_COPY_TO_USER,
				    SPIT_RPMSG_STREAM_TYPE_REQ_GET_CAPABILITIES,
				    NULL);
		if (ret)
			return ret;
		break;

	case SPIT_GET_MODE:
		/* Send request and wait mode */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
					sizeof(struct spit_mode),
					SPIT_IOCTL_COPY_BOTH,
					SPIT_RPMSG_STREAM_TYPE_REQ_GET_MODE,
					NULL);
		if (ret)
			return ret;
		break;

	case SPIT_GET_MODE_FPS:
		/* Send request and wait fps mode */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
					sizeof(struct spit_mode_fps),
					SPIT_IOCTL_COPY_BOTH,
					SPIT_RPMSG_STREAM_TYPE_REQ_GET_MODE_FPS,
					NULL);
		if (ret)
			return ret;
		break;

	case SPIT_GET_CONFIGURATION:
		/* Send stream configuration request and wait response */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				   sizeof(struct spit_stream_conf),
				   SPIT_IOCTL_COPY_BOTH,
				   SPIT_RPMSG_STREAM_TYPE_REQ_GET_CONFIGURATION,
				   NULL);
		if (ret)
			return ret;
		break;

	case SPIT_CONFIGURE_STREAM:
	case SPIT_SET_CONFIGURATION:
		/* Get frame types */
		if (copy_from_user(&frame_types, (void __user *) arg +
				 offsetof(struct spit_stream_conf, frame_types),
				 sizeof(frame_types)))
			return -EINVAL;

		/* Check if stream is started for frame type */
		if (spit_devdata->started & frame_types)
			return -EBUSY;

		/* Send stream configuration and wait completion */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				   sizeof(struct spit_stream_conf),
				   SPIT_IOCTL_COPY_BOTH,
				   SPIT_RPMSG_STREAM_TYPE_REQ_SET_CONFIGURATION,
				   NULL);
		if (ret)
			return ret;
		break;

	case SPIT_GET_FIFO:
	case SPIT_GET_INPUT_FIFO:
		/* Request FIFO size */
		ret = spit_request_fifo(spit_devdata,
					req == SPIT_GET_INPUT_FIFO,
					(void __user *) arg);
		if (ret)
			return ret;
		break;

	case SPIT_START_STREAM:
		/* Get frame types */
		if (copy_from_user(&frame_types, (void __user *) arg,
				   sizeof(frame_types)))
			return -EINVAL;

		/* Update started frame types
		 * This update must be done before request since some frame can
		 * arrive before the response of stream start request).
		 */
		mutex_lock(&spit_devdata->lock);
		spit_devdata->started |= frame_types;
		mutex_unlock(&spit_devdata->lock);

		/* Send start request and wait completion */
		ret = spit_send_request(spit_devdata, &frame_types,
					sizeof(frame_types),
					SPIT_IOCTL_COPY_NONE,
					SPIT_RPMSG_STREAM_TYPE_REQ_START,
					NULL);
		if (ret) {
			/* Update not started frame types */
			mutex_lock(&spit_devdata->lock);
			spit_devdata->started &= ~frame_types;
			if (!spit_devdata->started)
				spit_flush_buffers(spit_devdata);
			mutex_unlock(&spit_devdata->lock);
			return ret;
		}
		break;

	case SPIT_STOP_STREAM:
		/* Get frame types */
		if (copy_from_user(&frame_types, (void __user *) arg,
				   sizeof(frame_types)))
			return -EINVAL;

		/* Send stop request and wait completion */
		ret = spit_send_request(spit_devdata, &frame_types,
					sizeof(frame_types),
					SPIT_IOCTL_COPY_NONE,
					SPIT_RPMSG_STREAM_TYPE_REQ_STOP,
					NULL);
		if (ret)
			return ret;

		/* Update started frame types and flush frame FIFO */
		mutex_lock(&spit_devdata->lock);
		spit_devdata->started &= ~frame_types;
		if (!spit_devdata->started)
			spit_flush_buffers(spit_devdata);
		mutex_unlock(&spit_devdata->lock);
		break;

	case SPIT_CHANGE_BITRATE:
		/* Check H264 stream is started */
		mutex_lock(&spit_devdata->lock);
		if (!(spit_devdata->started & SPIT_FRAME_TYPES_H264)) {
			dev_err(spit_devdata->dev,
				"cannot change stream bitrate, not started\n");
			mutex_unlock(&spit_devdata->lock);
			return -EPERM;
		}
		mutex_unlock(&spit_devdata->lock);

		/* Send change bitrate request and wait completion */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				      sizeof(struct spit_bitrate_change),
				      SPIT_IOCTL_COPY_BOTH,
				      SPIT_RPMSG_STREAM_TYPE_REQ_CHANGE_BITRATE,
				      NULL);
		if (ret)
			return ret;
		break;

	case SPIT_CHANGE_FRAMERATE:
		/* Check H264 stream is started */
		mutex_lock(&spit_devdata->lock);
		if (!(spit_devdata->started & SPIT_FRAME_TYPES_H264)) {
			dev_err(spit_devdata->dev,
			        "cannot change stream framerate, not started\n");
			mutex_unlock(&spit_devdata->lock);
			return -EPERM;
		}
		mutex_unlock(&spit_devdata->lock);

		/* Send change framerate request and wait completion */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				    sizeof(struct spit_fps),
				    SPIT_IOCTL_COPY_BOTH,
				    SPIT_RPMSG_STREAM_TYPE_REQ_CHANGE_FRAMERATE,
				    NULL);
		if (ret)
			return ret;
		break;

	case SPIT_RELEASE_FRAME:
		/* Get frame type from frame desc */
		if (copy_from_user(&frame_type,
				   (void __user *) arg +
					 offsetof(struct spit_frame_desc, type),
				   sizeof(frame_type)))
			return -EINVAL;

		/* Check frame type */
		if (frame_type >= SPIT_FRAME_TYPE_COUNT)
			return -EINVAL;

		/* Only send a release frame request for RAW frames */
		if (!(spit_devdata->started & SPIT_FRAME_TYPES_RAW))
			return 0;

		/* Check if memory is mapped for this frame type */
		if (!spit_devdata->fifo_map[frame_type].vaddr)
			return -EFAULT;

		/**
		 * Do not send release frame rpmsg as threadx do not process it
		 */
#if 0
		/* Send frame release request and wait completion */
		ret = spit_send_request(spit_devdata, (void __user *) arg,
				       sizeof(struct spit_frame_desc),
				       SPIT_IOCTL_COPY_FROM_USER,
				       SPIT_RPMSG_STREAM_TYPE_REQ_FRAME_RELEASE,
				       NULL);
		if (ret)
			return ret;
#endif
		break;

	case SPIT_FEED_FRAME:
	{
		struct spit_fifo_map *map;
		off_t offset;

		/* Get frame desc from user */
		if (copy_from_user(&frame_desc, (void __user *) arg,
				   sizeof(frame_desc)))
			return -EINVAL;

		/* Check frame type */
		if (frame_desc.type >= SPIT_FRAME_TYPE_COUNT)
			return -EINVAL;

		mutex_lock(&spit_devdata->lock);

		/* Get input FIFO map for frame type */
		map = &spit_devdata->fifo_map[frame_desc.type +
							 SPIT_FRAME_TYPE_COUNT];
		if (!map->vaddr) {
			dev_err(spit_devdata->dev,
				"input FIFO memory is not mapped\n");
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}

		/* Check free size in input FIFO */
		if (frame_desc.size > spit_devdata->input_status.free_size) {
			dev_err(spit_devdata->dev,
				"not enough space in input FIFO\n");
			mutex_unlock(&spit_devdata->lock);
			return -ENOMEM;
		}

		/* Move virtual address to physical address */
		offset = (void *) spit_get_frame_data(&frame_desc) - map->vaddr;
		if (map->info.flags & SPIT_BUFFER_FIFO_FLAGS_TRUNC &&
		    offset > map->size)
			offset -= map->size;
		spit_set_frame_data(&frame_desc, map->paddr + offset);

		/* Check offset in input FIFO buffer */
		if (offset != spit_devdata->input_status.offset) {
			dev_err(spit_devdata->dev, "invalid offset in FIFO\n");
			mutex_unlock(&spit_devdata->lock);
			return -EINVAL;
		}

		/* Update free size in input status */
		spit_devdata->input_status.free_size -= frame_desc.size;

		mutex_unlock(&spit_devdata->lock);

		/* Send feed frame request and wait ack */
		ret = spit_send_request(spit_devdata, &frame_desc,
				        sizeof(frame_desc),
				        SPIT_IOCTL_COPY_NONE,
				        SPIT_RPMSG_STREAM_TYPE_REQ_FEED_FRAME,
				        NULL);
		if (ret)
			return ret;
		break;
	}
	case SPIT_GET_FRAME:
	{
		struct spit_fifo_map *map;
		off_t offset;

		mutex_lock(&spit_devdata->lock);

		/* Loop over fifo and started streams */
		do {
			/* No buffer available for now */
			ret = kfifo_len(&spit_file->frame_fifo);
			if (!ret) {
				mutex_unlock(&spit_devdata->lock);
				return -EAGAIN;
			}

			/* Get next frame from fifo */
			ret = kfifo_get(&spit_file->frame_fifo, &frame_desc);
			if (!ret) {
				mutex_unlock(&spit_devdata->lock);
				return -EFAULT;
			}
		} while (!SPIT_FRAME_TYPES_HAS_TYPE(spit_devdata->started,
						    frame_desc.type));

		/* Get FIFO map for frame type */
		map = &spit_devdata->fifo_map[frame_desc.type];
		if (!map->vaddr) {
			dev_err(spit_devdata->dev,
				"FIFO memory is not mapped\n");
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}

		/* Get offset of the frame */
		offset = (void *) spit_get_frame_data(&frame_desc) - map->paddr;

		/* Check offset and frame size */
		if (offset >= map->size || frame_desc.size > map->size) {
			dev_err(spit_devdata->dev, "drop invalid frame #%lu\n",
				(unsigned long) frame_desc.seqnum);
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}

		/* Update data address to user-mapped */
		spit_set_frame_data(&frame_desc, map->vaddr + offset);
		if (copy_to_user((void __user *) arg, &frame_desc,
				 sizeof(frame_desc))) {
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}

		/* Invalidate cache for frame */
		spit_invalidate_cache(spit_devdata, frame_desc.type,
				      map->paddr + offset, frame_desc.size);

		dev_dbg(spit_devdata->dev,
			"retrieved frame #%lu with vaddr 0x%p\n",
			(unsigned long) frame_desc.seqnum,
			spit_get_frame_data(&frame_desc));

		mutex_unlock(&spit_devdata->lock);
		break;
	}
	case SPIT_SET_CACHE:
	{
		struct spit_cache cache;

		if (copy_from_user(&cache, (void __user *) arg, sizeof(cache)))
			return -EFAULT;

		if (cache.type >= SPIT_FRAME_TYPE_COUNT * 2)
			return -EINVAL;

		mutex_lock(&spit_devdata->lock);
		if (spit_devdata->fifo_map[cache.type].vaddr) {
			dev_err(spit_devdata->dev, "can't change cache setting if device is already mapped\n");
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}
		mutex_unlock(&spit_devdata->lock);

		atomic_set(&spit_devdata->fifo_map[cache.type].cache_enabled,
			   !!cache.enable);
		break;
	}

	case SPIT_GET_FRAME_FIFO_DEPTH:
		if (copy_to_user((void __user *) arg,
				 &spit_file->frame_fifo_depth,
				 sizeof(spit_file->frame_fifo_depth)))
			return -EFAULT;
		break;

	case SPIT_SET_FRAME_FIFO_DEPTH:
	{
		unsigned int frame_fifo_depth;

		/* Cannot change FIFO depth when a frame type is mapped */
		if (spit_file->frame_types_mapped)
			return -EBUSY;

		/* Get frame FIFO depth */
		if (copy_from_user(&frame_fifo_depth, (void __user *) arg,
				   sizeof(frame_fifo_depth)))
			return -EFAULT;

		mutex_lock(&spit_devdata->lock);
		spit_file->frame_fifo_depth = frame_fifo_depth;
		mutex_unlock(&spit_devdata->lock);
		break;
	}

	case SPIT_GET_INPUT_STATUS:
		mutex_lock(&spit_devdata->lock);

		/* Copy last input status received */
		if (copy_to_user((void __user *) arg,
				 &spit_devdata->input_status,
				 sizeof(spit_devdata->input_status))) {
			mutex_unlock(&spit_devdata->lock);
			return -EFAULT;
		}

		mutex_unlock(&spit_devdata->lock);
		break;

	default:
		dev_err(spit_devdata->dev, "unknown command\n");
		return -EINVAL;
	}

	return 0;
}

static void spit_vma_close(struct vm_area_struct *vma)
{
	struct spit_file *spit_file = vma->vm_private_data;
	struct spit_devdata *spit_devdata = spit_file->spit_devdata;
	enum spit_frame_type type;

	type = vma->vm_pgoff;
	if (type >= SPIT_FRAME_TYPE_COUNT * 2)
		return;

	/* Lock memory mapping access */
	mutex_lock(&spit_devdata->lock);

	/* Remove virtual address from map */
	spit_devdata->fifo_map[type].vaddr = 0;

	/* Remove frame type from mapping */
	spit_file->frame_types_mapped &= ~SPIT_FRAME_TYPE_TO_TYPES(type);
	if (!spit_file->frame_types_mapped)
		kfifo_free(&spit_file->frame_fifo);
	spit_devdata->file_ref[type] = NULL;

	/* Unlock memory mapping access */
	mutex_unlock(&spit_devdata->lock);
}

static const struct vm_operations_struct spit_vm_ops = {
	.close = spit_vma_close,
};

static int spit_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct spit_file *spit_file = filp->private_data;
	struct spit_devdata *spit_devdata = spit_file->spit_devdata;
	struct spit_fifo_map *map;
	enum spit_frame_type type;
	size_t size, over_size, max_size;
	unsigned long pfn;

	/* Get frame type from offset */
	type = vma->vm_pgoff;
	if (type >= SPIT_FRAME_TYPE_COUNT * 2)
		return -EINVAL;

	/* Get mapping parameters */
	map = &spit_devdata->fifo_map[type];

	/* Lock memory mapping access */
	mutex_lock(&spit_devdata->lock);

	/* The FIFO is already mapped */
	if (map->vaddr) {
		dev_err(spit_devdata->dev, "device can't be mapped twice");
		mutex_unlock(&spit_devdata->lock);
		return -EBUSY;
	}

	/* Copy last FIFO info */
	if (!map->info.buf_size) {
		dev_err(spit_devdata->dev, "get FIFO size before using mmap()");
		mutex_unlock(&spit_devdata->lock);
		return -EAGAIN;
	}
	map->paddr = (void *) spit_get_buffer_fifo_phys(&map->info);
	map->size = map->info.buf_size;

	/* Check size of mapping */
	size = vma->vm_end - vma->vm_start;
	max_size = map->size;
	if (map->info.flags & SPIT_BUFFER_FIFO_FLAGS_TRUNC)
		max_size *= 2;
	if (size > max_size) {
		dev_err(spit_devdata->dev, "cannot map more than %lu bytes",
			max_size);
		mutex_unlock(&spit_devdata->lock);
		return -EINVAL;
	}

	pfn = ((unsigned long) map->paddr) >> PAGE_SHIFT;

	if (atomic_read(&map->cache_enabled) == 0) {
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
		dev_info(spit_devdata->dev, "cache disabled\n");
	} else {
		dev_info(spit_devdata->dev, "cache enabled\n");
	}

	/* Calculate mapping size */
	if (size > map->size) {
		over_size = size - map->size;
		size = map->size;
	} else
		over_size = 0;

	/* Map FIFO buffer */
	if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
		mutex_unlock(&spit_devdata->lock);
		return -EAGAIN;
	}

	/* Map twice the buffer to do overlap and avoid a copy for truncated
	 * frames.
	 */
	if (over_size &&
	    remap_pfn_range(vma, vma->vm_start + size, pfn, over_size,
			    vma->vm_page_prot)) {
		mutex_unlock(&spit_devdata->lock);
		return -EAGAIN;
	}

	/* Allocate frame FIFO for first mmap */
	if (!spit_file->frame_types_mapped) {
		if (kfifo_alloc(&spit_file->frame_fifo,
				spit_file->frame_fifo_depth, GFP_KERNEL)) {
			mutex_unlock(&spit_devdata->lock);
			return -ENOMEM;
		}
	}
	spit_file->frame_types_mapped |= SPIT_FRAME_TYPE_TO_TYPES(type);
	spit_devdata->file_ref[type] = spit_file;

	vma->vm_private_data = spit_file;
	vma->vm_ops = &spit_vm_ops;
	spit_devdata->fifo_map[type].vaddr = (void *) vma->vm_start;

	/* Unlock memory mapping access */
	mutex_unlock(&spit_devdata->lock);

	return 0;
}

static unsigned int spit_poll(struct file *filp, poll_table *wait)
{
	struct spit_file *spit_file = filp->private_data;
	struct spit_devdata *spit_devdata = spit_file->spit_devdata;
	int ret = 0;

	poll_wait(filp, &spit_devdata->data_wait, wait);

	/* Check if any buffer is available */
	if (kfifo_len(&spit_file->frame_fifo))
		ret |= POLLIN | POLLRDNORM;

	/* Check if input status report has been updated */
	if (atomic_read(&spit_devdata->input_status_updated)) {
		atomic_set(&spit_devdata->input_status_updated, 0);
		ret |= POLLOUT | POLLWRNORM;
	}

	return ret;
}

static const struct file_operations spit_fops = {
	.owner		= THIS_MODULE,
	.open		= spit_open,
	.release	= spit_close,
	.unlocked_ioctl	= spit_ioctl,
	.mmap		= spit_mmap,
	.poll		= spit_poll,
};

static int __init spit_init(void)
{
	int i;
	int ret = 0;
	int miscdev_register = 0;

	spit_devs = kcalloc(SPIT_STREAM_COUNT, sizeof(struct spit_devdata),
			     GFP_KERNEL);
	if (!spit_devs)
		return -ENOMEM;

	for (i = 0; i < SPIT_STREAM_COUNT; i++) {
		int j;

		mutex_init(&spit_devs[i].lock);

		for (j = 0; j < SPIT_RPMSG_STREAM_TYPE_RESP_COUNT; j++)
			mutex_init(&spit_devs[i].ctrl_lock[j]);

		spit_devs[i].stream = i;
		spit_devs[i].started = 0;
		spit_devs[i].initialized = 0;
		atomic_set(&spit_devs[i].opened, 0);
		init_waitqueue_head(&spit_devs[i].ctrl_wait);
		init_waitqueue_head(&spit_devs[i].data_wait);

		/* Creating /dev spit device */
		snprintf(spit_devs[i].devname, sizeof(spit_devs[i].devname),
			 MODULE_NAME "%d", i);
		spit_devs[i].misc_dev.minor = MISC_DYNAMIC_MINOR;
		spit_devs[i].misc_dev.name = spit_devs[i].devname;
		spit_devs[i].misc_dev.fops = &spit_fops;
		ret = misc_register(&spit_devs[i].misc_dev);
		if (ret < 0)
			goto failed;
		miscdev_register++;
		spit_devs[i].dev = spit_devs[i].misc_dev.this_device;

		/* Setup DMA mapping */
		arch_setup_dma_ops(spit_devs[i].dev, 0, 0, NULL, false);

		dev_info(spit_devs[i].dev,
			"device created successfully\n");
	}

	register_rpmsg_driver(&spit_rpmsg_driver);

	return 0;

failed:
	for (i = 0; i < miscdev_register; i++)
		misc_deregister(&spit_devs[i].misc_dev);
	kfree(spit_devs);

	return ret;
}

static void __exit spit_exit(void)
{
	int i;

	unregister_rpmsg_driver(&spit_rpmsg_driver);

	for (i = 0; i < SPIT_STREAM_COUNT; i++)
		misc_deregister(&spit_devs[i].misc_dev);
	kfree(spit_devs);
}

module_init(spit_init);
module_exit(spit_exit);

MODULE_DESCRIPTION("Parrot Spit Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aurelien Lefebvre <aurelien.lefebvre@parrot.com>");
