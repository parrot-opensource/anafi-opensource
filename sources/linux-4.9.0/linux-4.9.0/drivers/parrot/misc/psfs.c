/*
 * Parrot shared file system - file system rpmsg client driver
 *
 * Copyright (C) 2017 Parrot SA
 *
 * Alexandre Dilly <alexandre.dilly@parrot.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/fs.h>

#include <linux/slab.h>

#include <linux/psfs.h>

struct psfs {
	struct device		*dev;
	struct work_struct  work_cb;
	struct mutex        lock_mutex;
	struct list_head	queue;
	struct workqueue_struct *wq;
};

struct psfs_rpmsg2 {
	struct list_head entry;
	struct psfs_rpmsg msg[0];
};

/*
 * Virtual file system access helpers
 */
#define PSFS_VFS_BEGIN(fs) \
	do { fs = get_fs(); set_fs(get_ds()); } while (0)
#define PSFS_VFS_END(fs) \
	set_fs(fs)

/*
 * Shared memory mapping
 */
static inline void *psfs_map(psfs_addr_t addr)
{
	/* FIXME: currently, the memory used on ThreadX is already mapped in
	 * kernel space and we can access to by simply converting physical
	 * address to virtual address in kernel memory space.
	 */
	return phys_to_virt((phys_addr_t) psfs_to_ptr(addr));
}

static inline void psfs_unmap(void *ptr)
{
}

/*
 * File system callbacks
 */
static int psfs_cmd_open(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_open *params = (void *) &msg->params;
	struct file *filp;
	int flags = 0;
	int ret = 0;

	/* Translate flags */
	if (params->flags == PSFS_O_RDONLY)
		flags |= O_RDONLY;
	if (params->flags & PSFS_O_WRONLY)
		flags |= O_WRONLY;
	if (params->flags & PSFS_O_RDWR)
		flags |= O_RDWR;
	if (params->flags & PSFS_O_APPEND)
		flags |= O_APPEND;
	if (params->flags & PSFS_O_CREAT)
		flags |= O_CREAT;
	if (params->flags & PSFS_O_EXCL)
		flags |= O_EXCL;
	if (params->flags & PSFS_O_TRUNC)
		flags |= O_TRUNC;

	/* Open file */
	filp = filp_open(params->name, flags, 0600);
	if (IS_ERR(filp)) {
		dev_dbg(sfs->dev, "failed opening: %s\n", params->name);
		ret = PTR_ERR(filp);
	}

	dev_dbg(sfs->dev, "open: (%s, %x) -> %p\n", params->name, flags, filp);

	/* Set pointer reply with file pointer */
	psfs_set_reply_ptr(msg, filp);

	return ret;
}

static int psfs_cmd_read(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_io *params = (void *) &msg->params;
	struct file *filp;
	mm_segment_t fs;
	ssize_t len = -1;
	size_t count;
	void *buf;


	/* Check parameters */
	if (!params->filp || !params->buf)
		goto end;

	/* No bytes to read */
	if (!params->count) {
		len = 0;
		goto end;
	}

	/* Get parameters */
	filp = psfs_to_ptr(params->filp);
	count = (size_t) params->count;

	/* Map shared memory */
	buf = psfs_map(params->buf);

	/* Read data from file */
	PSFS_VFS_BEGIN(fs);
	len = vfs_read(filp, buf, count, &filp->f_pos);
	PSFS_VFS_END(fs);

	/* Unmap shared memory */
	psfs_unmap(buf);

	if (len < 0)
		dev_dbg(sfs->dev, "failed to read %ld\n", count);

	dev_dbg(sfs->dev, "read: (%p, %p, %lu) -> %ld\n",
		filp, buf, count, len);

end:
	/* Set ssize_t reply */
	psfs_set_reply_ssize(msg, len);
	return (int) len;
}

static int psfs_cmd_write(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_io *params = (void *) &msg->params;
	struct file *filp;
	mm_segment_t fs;
	ssize_t len = -1;
	size_t count;
	void *buf;

	/* Check parameters */
	if (!params->filp || !params->buf)
		goto end;

	/* No bytes to write */
	if (!params->count) {
		len = 0;
		goto end;
	}

	/* Get parameters */
	filp = psfs_to_ptr(params->filp);
	count = (size_t) params->count;

	/* Map shared memory */
	buf = psfs_map(params->buf);

	/* Write data to file */
	PSFS_VFS_BEGIN(fs);
	len = vfs_write(filp, buf, count, &filp->f_pos);
	PSFS_VFS_END(fs);

	/* Unmap shared memory */
	psfs_unmap(buf);

	if (len < 0)
		dev_dbg(sfs->dev, "failed to write %ld\n", count);

	dev_dbg(sfs->dev, "write: (%p, %p, %lu) -> %ld\n",
		filp, buf, count, len);

end:
	/* Set ssize_t reply */
	psfs_set_reply_ssize(msg, len);
	return (int) len;
}

static int psfs_cmd_seek(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_seek *params = (void *) &msg->params;
	struct file *filp;
	loff_t offset = -1;
	int whence;

	/* Check parameters */
	if (!params->filp)
		goto end;

	/* Get parameters */
	filp = psfs_to_ptr(params->filp);
	offset = (loff_t) params->offset;
	switch (params->whence) {
	case PSFS_SEEK_SET:
		whence = SEEK_SET;
		break;
	case PSFS_SEEK_CUR:
		whence = SEEK_CUR;
		break;
	case PSFS_SEEK_END:
		whence = SEEK_END;
		break;
	default:
		whence = params->whence;
	}

	/* Seek in file */
	offset = vfs_llseek(filp, offset, whence);

	if (offset < 0)
		dev_dbg(sfs->dev, "failed to seek\n");

	dev_dbg(sfs->dev, "seek: (%p, %lld, %d) -> %lld\n",
		filp, (long long) params->offset, whence, offset);

end:
	/* Set loff_t reply */
	psfs_set_reply_loff(msg, offset);
	return (int) offset;
}

static int psfs_cmd_close(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_close *params = (void *) &msg->params;
	struct file *filp;
	int ret;

	/* Get file pointer */
	filp = psfs_to_ptr(params->filp);

	/* Set void reply */
	psfs_set_reply_void(msg);

	/* Close file */
	if (filp) {
		ret = vfs_fsync(filp, 0);
		if (ret < 0)
			dev_dbg(sfs->dev, "close: sync failure %d\n", ret);

		filp_close(filp, NULL);
	}

	dev_dbg(sfs->dev, "close: %p\n", filp);

	return 0;
}

static int psfs_cmd_stat(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_stat *params = (void *) &msg->params;
	struct psfs_stat *buf = NULL;
	struct kstat kbuf;
	mm_segment_t fs;
	int ret;

	/* Check stat buffer */
	if (!params->buf)
		return -EINVAL;

	/* Get kstat from file */
	PSFS_VFS_BEGIN(fs);
	ret = vfs_stat(params->name, &kbuf);
	PSFS_VFS_END(fs);

	/* Fill stat buffer */
	if (!ret) {
		/* Map shared memory */
		buf = psfs_map(params->buf);

		/* Copy values */
		buf->dev = kbuf.dev;
		buf->ino = kbuf.ino;
		buf->mode = kbuf.mode;
		buf->nlink = kbuf.nlink;
		buf->uid = kbuf.uid.val;
		buf->gid = kbuf.gid.val;
		buf->rdev = kbuf.rdev;
		buf->size = kbuf.size;
		buf->blksize = kbuf.blksize;
		buf->blocks = kbuf.blocks;
		buf->atim_sec = kbuf.atime.tv_sec;
		buf->atim_nsec = kbuf.atime.tv_nsec;
		buf->mtim_sec = kbuf.mtime.tv_sec;
		buf->mtim_nsec = kbuf.mtime.tv_nsec;
		buf->ctim_sec = kbuf.ctime.tv_sec;
		buf->ctim_nsec = kbuf.ctime.tv_nsec;

		/* Unmap shared memory */
		psfs_unmap(buf);
	}

	if (ret < 0)
		dev_dbg(sfs->dev, "failed to stat %s\n", params->name);

	dev_dbg(sfs->dev, "stat: (%s, %p) -> %d\n", params->name, buf, ret);

	/* Set code return reply */
	psfs_set_reply_code(msg, ret);

	return ret;
}

static int psfs_cmd_remove(struct psfs *sfs, struct psfs_rpmsg *msg)
{
	struct psfs_params_remove *params = (void *) &msg->params;
	mm_segment_t fs;
	int ret;

	/* Remove file or directory */
	PSFS_VFS_BEGIN(fs);
	ret = sys_unlink(params->name);
	PSFS_VFS_END(fs);

	if (ret < 0)
		dev_dbg(sfs->dev, "failed to remove %s\n", params->name);

	dev_dbg(sfs->dev, "remove: (%s) -> %d\n", params->name, ret);

	/* Set code return reply */
	psfs_set_reply_code(msg, ret);

	return ret;
}

static struct {
	const char *name;
	int (*cb)(struct psfs *sfs, struct psfs_rpmsg *msg);
} psfs_cmds[PSFS_CMD_COUNT] = {
	/* File functions */
	[PSFS_CMD_OPEN] = { "OPEN", psfs_cmd_open },
	[PSFS_CMD_READ] = { "READ", psfs_cmd_read },
	[PSFS_CMD_WRITE] = { "WRITE", psfs_cmd_write },
	[PSFS_CMD_SEEK] = { "SEEK", psfs_cmd_seek },
	[PSFS_CMD_CLOSE] = { "CLOSE", psfs_cmd_close },

	/* File system functions */
	[PSFS_CMD_STAT] = { "STAT", psfs_cmd_stat },
	[PSFS_CMD_REMOVE] = { "REMOVE", psfs_cmd_remove },
};

static void psfs_work_cb(struct work_struct *ws)
{
	int ret;
	struct psfs *sfs = container_of(ws, struct psfs, work_cb);
	struct psfs_rpmsg2 *msg2;
	struct psfs_rpmsg *msg;
	struct device *dev;
	struct rpmsg_device *rpdev;

	while (1) {
		mutex_lock(&sfs->lock_mutex);
		msg2 = list_first_entry_or_null(&sfs->queue, struct psfs_rpmsg2, entry);
		if (msg2)
			list_del(&msg2->entry);
		mutex_unlock(&sfs->lock_mutex);

		if (!msg2)
			break;

		msg = &msg2->msg[0];
		dev = sfs->dev;
		rpdev = container_of(dev, struct rpmsg_device, dev);

		/* Check psfs command */
		if (msg->cmd >= PSFS_CMD_COUNT) {
			dev_err(&rpdev->dev, "invalid command\n");
		} else {
			/* Call command callback */
			dev_dbg(&rpdev->dev, "exec cmd %s id %d", psfs_cmds[msg->cmd].name, msg->id);
			ret = psfs_cmds[msg->cmd].cb(sfs, msg);
			dev_dbg(&rpdev->dev, "cmd %s id %d status (%d)",
						psfs_cmds[msg->cmd].name, msg->id, ret);
			/* rpmsg_send will copy msg in shared ring buffer */
			rpmsg_send(rpdev->ept, msg, sizeof(*msg));
		}
		kfree(msg2);
	}
}

/*
 * Callback of rpmsg driver
 */
static int psfs_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			 void *priv, u32 src)
{
	struct psfs *sfs = dev_get_drvdata(&rpdev->dev);
	struct psfs_rpmsg *msg = data;

	/* Check rpmsg length */
	if (len < sizeof(*msg)) {
		dev_err(&rpdev->dev, "invalid message length\n");
		return -EBADMSG;
	}

	/* Check psfs command */
	if (msg->cmd >= PSFS_CMD_COUNT) {
		dev_err(&rpdev->dev, "invalid command\n");
		return -EINVAL;
	}

	/* Process command */
	if (psfs_cmds[msg->cmd].cb) {
		/* queue the message for delay processing */
		/* msg will be recycled when we return from this callback
		 *  copy it
		 */
		struct psfs_rpmsg2 *msg2;

		msg2 = kzalloc(len + sizeof(*msg2), GFP_KERNEL);
		if (!msg2)
			return -ENOMEM; /* should we send a void reply ? */

		memcpy(&msg2->msg, msg, len);
		mutex_lock(&sfs->lock_mutex);
		list_add_tail(&msg2->entry, &sfs->queue);
		mutex_unlock(&sfs->lock_mutex);

		queue_work(sfs->wq, &sfs->work_cb);
	} else {
		/* Set a void reply */
		psfs_set_reply_void(msg);
		/* Send reply */
		rpmsg_send(rpdev->ept, msg, sizeof(*msg));
	}


	return 0;
}

/*
 * File system events
 */
#ifdef CONFIG_SYSFS
static ssize_t psfs_rootfs_mounted(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	static int is_mounted = 0;
	struct psfs_rpmsg msg;

	/* Rootfs mount event already sent */
	if (is_mounted)
		return -1;
	is_mounted = 1;

	/* Roots is now available */
	msg.cmd = PSFS_CMD_ROOTFS_MOUNTED;
	rpmsg_send(rpdev->ept, &msg, PSFS_CMD_ROOTFS_MOUNTED_SIZE);

	return count;
}

static ssize_t psfs_fs_mounted(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	unsigned char data[PSFS_PATH_MAX_LENGTH + sizeof(struct psfs_rpmsg)];
	struct psfs_rpmsg *msg = (struct psfs_rpmsg *) data;

	/* Cannot send a long path */
	if (count + 1 > PSFS_PATH_MAX_LENGTH)
		return -1;

	/* A new device has been mounted */
	msg->cmd = PSFS_CMD_FS_MOUNTED;
	strncpy(msg->path, buf, count);
	msg->path[count] = '\0';

	/* Remove possible line return */
	if (count && msg->path[count-1] == '\n')
		msg->path[count-1] = '\0';

	/* Send FS mounted event */
	rpmsg_send(rpdev->ept, msg, PSFS_CMD_FS_MOUNTED_SIZE(count));

	return count;
}

static ssize_t psfs_fs_umounted(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct rpmsg_device *rpdev = container_of(dev, struct rpmsg_device,
						  dev);
	unsigned char data[PSFS_PATH_MAX_LENGTH + sizeof(struct psfs_rpmsg)];
	struct psfs_rpmsg *msg = (struct psfs_rpmsg *) data;

	/* Cannot send a long path */
	if (count + 1 > PSFS_PATH_MAX_LENGTH)
		return -1;

	/* One of mounted device has been unmounted */
	msg->cmd = PSFS_CMD_FS_UNMOUNTED;
	strncpy(msg->path, buf, count);
	msg->path[count] = '\0';

	/* Remove possbile line return */
	if (msg->path[count-1] == '\n')
		msg->path[count-1] = '\0';

	/* Send FS unmounted event */
	rpmsg_send(rpdev->ept, msg, PSFS_CMD_FS_UNMOUNTED_SIZE(count));

	return count;
}

static struct device_attribute psfs_sysfs_attrs[] = {
	__ATTR(rootfs_mounted, S_IWUSR, NULL, psfs_rootfs_mounted),
	__ATTR(fs_mounted, S_IWUSR, NULL, psfs_fs_mounted),
	__ATTR(fs_umounted, S_IWUSR, NULL, psfs_fs_umounted),
};
#endif

/*
 * Device probe
 */
static int psfs_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;
	struct psfs *sfs;
#ifdef CONFIG_SYSFS
	int i;
#endif

	dev_info(&rpdev->dev, "device probe\n");
	/* Allocate private data structure */
	sfs = devm_kzalloc(&rpdev->dev, sizeof(*sfs), GFP_KERNEL);
	if (!sfs)
		return -ENOMEM;

	dev_set_drvdata(&rpdev->dev, sfs);

	/* Initialize structure */
	sfs->dev = &rpdev->dev;

	INIT_LIST_HEAD(&sfs->queue);
	INIT_WORK(&sfs->work_cb, psfs_work_cb);
	mutex_init(&sfs->lock_mutex);
	sfs->wq = alloc_workqueue("psfs_wq", WQ_FREEZABLE | WQ_MEM_RECLAIM, 0);

	/* Prepare channel info for remote processor */
	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	/* Send channel info to remote processor */
	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

#ifdef CONFIG_SYSFS
	/* Add sysfs entries */
	for (i = 0; i < ARRAY_SIZE(psfs_sysfs_attrs); i++)
		device_create_file(sfs->dev, &psfs_sysfs_attrs[i]);
#endif

	dev_info(&rpdev->dev, "device created successfully\n");

	return 0;
}

static void psfs_rpmsg_remove(struct rpmsg_device *rpdev)
{
	struct psfs *sfs = dev_get_drvdata(&rpdev->dev);
#ifdef CONFIG_SYSFS
	int i;

	/* Remove sysfs entries */
	for (i = 0; i < ARRAY_SIZE(psfs_sysfs_attrs); i++)
		device_remove_file(&rpdev->dev, &psfs_sysfs_attrs[i]);
#endif
	flush_workqueue(sfs->wq);
	destroy_workqueue(sfs->wq);
}

static struct rpmsg_device_id psfs_rpmsg_id_table[] = {
	{ .name	= PSFS_RPMSG_CHANNEL, },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, psfs_rpmsg_id_table);

static struct rpmsg_driver psfs_rpmsg_driver = {
	.drv = {
		.name	= KBUILD_MODNAME,
	},
	.id_table	= psfs_rpmsg_id_table,
	.probe		= psfs_rpmsg_probe,
	.callback	= psfs_rpmsg_cb,
	.remove		= psfs_rpmsg_remove,
};
module_rpmsg_driver(psfs_rpmsg_driver);

MODULE_DESCRIPTION("Parrot Shared Filesystem Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alexandre Dilly <alexandre.dilly@parrot.com>");
