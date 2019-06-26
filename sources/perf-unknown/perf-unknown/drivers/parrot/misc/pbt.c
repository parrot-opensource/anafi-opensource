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
#include <linux/timer.h>
#include <linux/pbt_interface.h>

#define MODULE_NAME "pbt"
#define DRIVER_NAME MODULE_NAME

#define PBT_PING_INTERVAL	CONFIG_MISC_PBT_PING_INTERVAL_MS
#define PBT_PING_TIMEOUT	CONFIG_MISC_PBT_PING_TIMEOUT_MS

struct pbt_drvdata {
	struct rpmsg_device	*rp_device;
	struct timer_list	ping_timer;
	uintptr_t		mem_addr;
	int			mem_size;
	struct miscdevice	misc_dev;
	struct device		*dev;
	atomic_t		opened;
	atomic_t		pong_wait;
	void			*mem;
	int			read_offset;
	wait_queue_head_t	crash_wait;
	int			crashed;
	struct work_struct work;
};

static struct pbt_drvdata *pbt_data;

static void pbt_rpmsg_work(struct work_struct *work)
{
	struct pbt_rpmsg rpmsg;
	rpmsg.type = PBT_RPMSG_TYPE_PING;
	rpmsg_send(pbt_data->rp_device->ept, &rpmsg,
			sizeof(struct pbt_rpmsg));
	/* rearm the timer here. It allow to be robust against
	   stuck workqueue */
	mod_timer(&pbt_data->ping_timer,
			jiffies + msecs_to_jiffies(PBT_PING_INTERVAL));
}

static void pbt_timer_ping(unsigned long data)
{
	/* If no reponse for a certain amount of time, we considered that
	 * threadx has crashed and wake-up potential char device listeners */
	if ((atomic_inc_return(&pbt_data->pong_wait) * PBT_PING_INTERVAL)
				>= PBT_PING_TIMEOUT) {
		dev_info(pbt_data->dev, "no reponse from threadx for %dms\n",
			 PBT_PING_TIMEOUT);
		pbt_data->crashed = 1;
		wake_up(&pbt_data->crash_wait);
	} else {
		schedule_work(&pbt_data->work);
	}
}


static int pbt_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	struct pbt_rpmsg *rpmsg = data;

	if (len != sizeof(struct pbt_rpmsg)) {
		dev_err(pbt_data->dev,
			"received message with invalid length\n");
		return -EINVAL;
	}

	switch (rpmsg->type) {
	case PBT_RPMSG_TYPE_MEM_INFO:
		pbt_data->mem_addr = pbt_get_addr(&rpmsg->meminfo);
		pbt_data->mem_size = rpmsg->meminfo.size;
		dev_info(pbt_data->dev, "received meminfo: 0x%p (%d bytes)\n",
			 (void *)pbt_data->mem_addr, pbt_data->mem_size);

		/* As the meminfo data has been received, we can start to
		 * ping threadx to watch if it's still alive */
		mod_timer(&pbt_data->ping_timer,
			 jiffies + msecs_to_jiffies(PBT_PING_INTERVAL));
		break;
	case PBT_RPMSG_TYPE_PONG:
		atomic_dec(&pbt_data->pong_wait);
		break;
	default:
		dev_err(pbt_data->dev, "received invalid rpmsg type %d\n",
			rpmsg->type);
		break;
	}

	return 0;
}

static int pbt_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	pbt_data->rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void pbt_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id pbt_rpmsg_id_table[] = {
	{ .name	= PBT_RPMSG_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, pbt_rpmsg_id_table);

static struct rpmsg_driver pbt_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= pbt_rpmsg_id_table,
	.probe		= pbt_rpmsg_probe,
	.callback	= pbt_rpmsg_cb,
	.remove		= pbt_rpmsg_remove,
};

static int pbt_open(struct inode *inode, struct file *filp)
{
	if (atomic_add_unless(&pbt_data->opened, 1, 1) == 0) {
		dev_err(pbt_data->dev, "device can't be opened twice\n");
		return -EBUSY;
	}

	if (!pbt_data->mem_addr || !pbt_data->mem_size)
		return -EAGAIN;

	/* We use simply phys_to_virt() since all ThreadX memory is already
	 * mapped into kernel space by ambalink driver (ppm2 memory).
	 */
	pbt_data->mem = phys_to_virt(pbt_data->mem_addr);
	if (!pbt_data->mem)
		return -EAGAIN;

	pbt_data->read_offset = 0;

	return 0;
}

static int pbt_release(struct inode *inode, struct file *filp)
{
	pbt_data->mem = NULL;
	atomic_set(&pbt_data->opened, 0);

	return 0;
}


static ssize_t pbt_read(struct file *fp, char __user *ubuf,
				  size_t cnt, loff_t *ppos)
{
	/* Read backtrace from shared memory */

	cnt = min(cnt, (size_t)pbt_data->mem_size - pbt_data->read_offset);

	if (copy_to_user(ubuf, (char *)pbt_data->mem + pbt_data->read_offset,
			   cnt))
		return -EFAULT;

	pbt_data->read_offset += cnt;
	*ppos += cnt;

	return cnt;
}

static unsigned int pbt_poll(struct file *filp, poll_table *wait)
{
	poll_wait(filp, &pbt_data->crash_wait, wait);

	if (pbt_data->crashed != 0)
		return POLLIN | POLLRDNORM;

	return 0;
}

static const struct file_operations pbt_fops = {
	.owner		= THIS_MODULE,
	.open		= pbt_open,
	.release	= pbt_release,
	.read		= pbt_read,
	.poll		= pbt_poll,
};

static int __init pbt_init(void)
{
	int ret;

	pbt_data = kzalloc(sizeof(struct pbt_drvdata), GFP_KERNEL);
	if (!pbt_data)
		return -ENOMEM;

	pbt_data->misc_dev.minor = MISC_DYNAMIC_MINOR;
	pbt_data->misc_dev.name = MODULE_NAME;
	pbt_data->misc_dev.fops = &pbt_fops;

	ret = misc_register(&pbt_data->misc_dev);
	if (ret < 0)
		goto failed;

	pbt_data->dev = pbt_data->misc_dev.this_device;
	atomic_set(&pbt_data->opened, 0);
	atomic_set(&pbt_data->pong_wait, 0);
	init_waitqueue_head(&pbt_data->crash_wait);

	pbt_data->ping_timer.function = pbt_timer_ping;
	init_timer(&pbt_data->ping_timer);
	INIT_WORK(&pbt_data->work, pbt_rpmsg_work);

	register_rpmsg_driver(&pbt_rpmsg_driver);

	return 0;

failed:
	kfree(pbt_data);
	return ret;
}

static void __exit pbt_exit(void)
{
	unregister_rpmsg_driver(&pbt_rpmsg_driver);
	misc_deregister(&pbt_data->misc_dev);
	kfree(pbt_data);
	pbt_data = NULL;
}

module_init(pbt_init);
module_exit(pbt_exit);

MODULE_DESCRIPTION("Parrot Backtrace (pbt) Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aurelien Lefebvre <aurelien.lefebvre@parrot.com>");
