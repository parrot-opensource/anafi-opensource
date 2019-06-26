/*
 *  @Copyright      :: Copyright (C) 2017 Parrot SA
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>

#define MODULE_NAME "pclock_test"
#define DRIVER_NAME MODULE_NAME

#define CLOCK_RPMSG_CHANNEL "clock_test"
struct pclock_test_rpmsg {
	uint64_t raw_clock;
	uint64_t monotonic_clock;
};

struct pclock_test_drvdata {
	struct rpmsg_device	*rp_device;
	spinlock_t		lock;
};

static struct pclock_test_drvdata *pclock_test_data;

static int pclock_test_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	struct pclock_test_rpmsg *rpmsg = data;
	unsigned long flags;
	struct timespec ts;

	if (len != sizeof(struct pclock_test_rpmsg)) {
		dev_err(&rpdev->dev, "received message with invalid length\n");
		return -EINVAL;
	}


	/* we return to threadx linux monotonic time and the
	   raw timer. We take a spin_lock_irqsave to
	   avoid to be preempted
	 */
	spin_lock_irqsave(&pclock_test_data->lock, flags);

	rpmsg->raw_clock = arch_counter_get_cntvct();
	ktime_get_ts(&ts);
	rpmsg->monotonic_clock = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
	spin_unlock_irqrestore(&pclock_test_data->lock, flags);

	rpmsg_send(pclock_test_data->rp_device->ept, rpmsg,
			sizeof(struct pclock_test_rpmsg));

	return 0;
}

static int pclock_test_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	pclock_test_data->rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void pclock_test_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id pclock_test_rpmsg_id_table[] = {
	{ .name	= CLOCK_RPMSG_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, pclock_test_rpmsg_id_table);

static struct rpmsg_driver pclock_test_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= pclock_test_rpmsg_id_table,
	.probe		= pclock_test_rpmsg_probe,
	.callback	= pclock_test_rpmsg_cb,
	.remove		= pclock_test_rpmsg_remove,
};

static int __init pclock_test_init(void)
{
	pclock_test_data = kzalloc(sizeof(struct pclock_test_drvdata), GFP_KERNEL);
	if (!pclock_test_data)
		return -ENOMEM;
	spin_lock_init(&pclock_test_data->lock);

	register_rpmsg_driver(&pclock_test_rpmsg_driver);

	return 0;
}

static void __exit pclock_test_exit(void)
{
	unregister_rpmsg_driver(&pclock_test_rpmsg_driver);
	kfree(pclock_test_data);
}

module_init(pclock_test_init);
module_exit(pclock_test_exit);

MODULE_DESCRIPTION("Parrot Shared Memory (pclock_test) Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Parrot SA");
