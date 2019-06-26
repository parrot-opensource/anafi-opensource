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
#include <linux/pshm_interface.h>

#define MODULE_NAME "pshm"
#define DRIVER_NAME MODULE_NAME

enum pshm_state {
	PSHM_STATE_IDLE,	/* Waiting for request */
	PSHM_STATE_REQUESTED,	/* A request is in progress, wait
				 * for response from ThreadX
				 */
	PSHM_STATE_ANSWERED,	/* ThreadX answered success */
	PSHM_STATE_FAILED,	/* ThreadX answered failure */

	PSHM_STATE_COUNT,
};

struct pshm_drvdata {
	struct rpmsg_device	*rp_device;
	struct miscdevice	misc_dev;
	struct device		*dev;
	atomic_t		opened;
	enum pshm_state		state;
	int			err;
	struct mutex		lock;
	wait_queue_head_t	wait_rpmsg;
	struct pshm_meminfo	current_mem;
	u32			current_seqnum;
	u32			next_seqnum;
};

static struct pshm_drvdata *pshm_data;

static int pshm_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	struct pshm_rpmsg *rpmsg = data;

	if (len != sizeof(struct pshm_rpmsg)) {
		dev_err(&rpdev->dev, "received message with invalid length\n");
		return -EINVAL;
	}

	/* Ignore received message if not in requested state */
	if (pshm_data->state != PSHM_STATE_REQUESTED) {
		dev_warn(&rpdev->dev, "%s: received unknown rpmsg\n",
                         __func__);
		return -EINVAL;
	}
	if (rpmsg->seqnum != pshm_data->current_seqnum) {
		dev_warn(&rpdev->dev, "%s: rpmsg seqnum mismatch, expected %u,"
			 " received %u\n", __func__, pshm_data->current_seqnum,
			 rpmsg->seqnum);
		return -EINVAL;
	}

	switch (rpmsg->type) {
	case PSHM_RPMSG_TYPE_MEMINFO:
		dev_dbg(pshm_data->dev,
			"received meminfo: %s @ %p (%u bytes) (%scacheable) %s\n",
			rpmsg->meminfo.name,
			(void *) pshm_get_addr(&rpmsg->meminfo),
			rpmsg->meminfo.size,
			rpmsg->meminfo.cache == PSHM_CACHE_MODE_CACHEABLE ?
								    "" : "not ",
			rpmsg->meminfo.new_alloc ? "(new_alloc)" : "");
		memcpy(&pshm_data->current_mem, &rpmsg->meminfo,
		       sizeof(struct pshm_meminfo));
		pshm_data->state = PSHM_STATE_ANSWERED;
		wake_up(&pshm_data->wait_rpmsg);
		break;
	case PSHM_RPMSG_TYPE_FAILED:
		dev_dbg(pshm_data->dev, "received failed response\n");
		pshm_data->state = PSHM_STATE_FAILED;
		pshm_data->err = rpmsg->err;
		wake_up(&pshm_data->wait_rpmsg);
		break;
	default:
		break;
	}

	return 0;
}

static int pshm_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	pshm_data->rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void pshm_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id pshm_rpmsg_id_table[] = {
	{ .name	= PSHM_RPMSG_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, pshm_rpmsg_id_table);

static struct rpmsg_driver pshm_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= pshm_rpmsg_id_table,
	.probe		= pshm_rpmsg_probe,
	.callback	= pshm_rpmsg_cb,
	.remove		= pshm_rpmsg_remove,
};

static long pshm_ioctl(struct file *filep, unsigned int req,
		unsigned long arg)
{
	int ret;
	struct pshm_rpmsg rpmsg;

	switch (req) {
	case PSHM_IOCTL_CREATE:
	case PSHM_IOCTL_CREATE_2:
	case PSHM_IOCTL_GET:
		mutex_lock(&pshm_data->lock);
		if (copy_from_user(&rpmsg.meminfo, (void __user *)arg,
				   sizeof(struct pshm_meminfo))) {
			ret = -EFAULT;
			goto error;
		}

		if (!pshm_data->rp_device) {
			ret = -ENODEV;
			goto error;
		}

		pshm_data->state = PSHM_STATE_REQUESTED;
		if (req == PSHM_IOCTL_CREATE || req == PSHM_IOCTL_CREATE_2) {
			rpmsg.type = PSHM_RPMSG_TYPE_CREATE;

			/* Legacy create do not use cache */
			if (req == PSHM_IOCTL_CREATE)
				rpmsg.meminfo.cache =
					PSHM_CACHE_MODE_NOT_CACHEABLE;

			if (rpmsg.meminfo.cache < 0 ||
			    rpmsg.meminfo.cache >= PSHM_CACHE_MODE_COUNT) {
				ret = -EINVAL;
				goto error;
			}

			dev_dbg(pshm_data->dev,
				"create request for '%s' (%u bytes) (%scacheable)\n",
				rpmsg.meminfo.name, rpmsg.meminfo.size,
				rpmsg.meminfo.cache ? "" : "not ");
		} else {
			rpmsg.type = PSHM_RPMSG_TYPE_GET;
			dev_dbg(pshm_data->dev,
				"get request for '%s'\n", rpmsg.meminfo.name);
		}
		rpmsg.seqnum = pshm_data->next_seqnum++;
		pshm_data->current_seqnum = rpmsg.seqnum;
		rpmsg_send(pshm_data->rp_device->ept, &rpmsg,
			   sizeof(struct pshm_rpmsg));

		/* The request has been sent to ThreadX, now wait for rpmsg
		 * response before continuing
		 */
		while (pshm_data->state != PSHM_STATE_ANSWERED &&
				pshm_data->state != PSHM_STATE_FAILED) {

			ret = wait_event_interruptible_timeout(
				pshm_data->wait_rpmsg,
				pshm_data->state == PSHM_STATE_ANSWERED ||
					pshm_data->state == PSHM_STATE_FAILED,
				5000);


			/* In case of timeout/interrupt while waiting for
			 * ThreadX, return to the idle state
			 */
			if (ret <= 0) {
				ret = ret ? -ERESTARTSYS : -ETIMEDOUT;
				goto error;
			}
		}

		if (pshm_data->state == PSHM_STATE_FAILED) {
			ret = pshm_data->err;
			goto error;
		}

		if (copy_to_user((void __user *)arg, &pshm_data->current_mem,
				 sizeof(struct pshm_meminfo))) {
			ret = -EFAULT;
			goto error;
		}

		ret = 0;
error:
		/* Return to idle state to be ready to handle future requests */
		pshm_data->state = PSHM_STATE_IDLE;
		mutex_unlock(&pshm_data->lock);
		return ret;
	default:
		dev_err(pshm_data->dev, "unknown command\n");
	}
	return 0;
}

static const struct file_operations pshm_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= pshm_ioctl,
};

static int __init pshm_init(void)
{
	int ret;

	pshm_data = kzalloc(sizeof(struct pshm_drvdata), GFP_KERNEL);
	if (!pshm_data)
		return -ENOMEM;

	pshm_data->misc_dev.minor = MISC_DYNAMIC_MINOR;
	pshm_data->misc_dev.name = "pshm";
	pshm_data->misc_dev.fops = &pshm_fops;
	ret = misc_register(&pshm_data->misc_dev);
	if (ret < 0)
		goto misc_register_failed;

	mutex_init(&pshm_data->lock);
	atomic_set(&pshm_data->opened, 0);
	init_waitqueue_head(&pshm_data->wait_rpmsg);
	pshm_data->dev = pshm_data->misc_dev.this_device;
	pshm_data->state = PSHM_STATE_IDLE;

	dev_info(pshm_data->dev,
		"device created successfully\n");

	register_rpmsg_driver(&pshm_rpmsg_driver);

	return 0;

misc_register_failed:
	kfree(pshm_data);

	return ret;
}

static void __exit pshm_exit(void)
{
	unregister_rpmsg_driver(&pshm_rpmsg_driver);
	misc_deregister(&pshm_data->misc_dev);
	kfree(pshm_data);
}

module_init(pshm_init);
module_exit(pshm_exit);

MODULE_DESCRIPTION("Parrot Shared Memory (pshm) Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aurelien Lefebvre <aurelien.lefebvre@parrot.com>");
