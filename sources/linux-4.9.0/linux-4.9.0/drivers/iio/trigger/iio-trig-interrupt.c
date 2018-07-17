/*
 * Industrial I/O - generic interrupt based trigger support
 *
 * Copyright (c) 2008-2013 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/slab.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>

struct iio_interrupt_trigger_info {
	struct kernfs_node *poll;
	atomic_t count;
	unsigned int irq;
};

ssize_t iio_interrupt_trigger_show_count(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	struct iio_trigger *trig = to_iio_trigger(dev);
	struct iio_interrupt_trigger_info *info = iio_trigger_get_drvdata(trig);
	unsigned int count = atomic_read(&info->count);

	return snprintf(buf, PAGE_SIZE, "%u\n", count);
}

static DEVICE_ATTR(count, S_IRUGO, iio_interrupt_trigger_show_count, NULL);

static struct attribute *iio_interrupt_trigger_attrs[] = {
	&dev_attr_count.attr,
	NULL,
};

static const struct attribute_group iio_interrupt_trigger_attr_group = {
	.attrs = iio_interrupt_trigger_attrs,
};

static const struct attribute_group *iio_interrupt_trigger_attr_groups[] = {
	&iio_interrupt_trigger_attr_group,
	NULL
};

static irqreturn_t iio_interrupt_trigger_poll(int irq, void *private)
{
	struct iio_trigger *trig = private;
	struct iio_interrupt_trigger_info *info = iio_trigger_get_drvdata(trig);

#if 0
/* TODO: remove me OR adapt code to acknowledge threadx imu interrupt here */
#if defined(CONFIG_ARM_GIC)
	/*amba_writel(AHB_SCRATCHPAD_REG(0x14), 0x1 << (irq - AXI_SOFT_IRQ(0)));*/
#else
	/*amba_writel(AMBALINK_VIC_REG(VIC_SOFT_INT_CLR_INT_OFFSET), irq % 32);*/
#endif
#endif

	atomic_inc(&info->count);
	sysfs_notify_dirent(info->poll);
	iio_trigger_poll(trig);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops iio_interrupt_trigger_ops = {
	.owner = THIS_MODULE,
};

static int iio_interrupt_trigger_probe(struct platform_device *pdev)
{
	struct iio_interrupt_trigger_info *trig_info;
	struct iio_trigger *trig;
	struct resource *irq_res;
	int irq, ret = 0;

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_res == NULL)
		return -ENODEV;
	irq = irq_res->start;

	trig = iio_trigger_alloc("irqtrig%d", irq);
	if (!trig) {
		ret = -ENOMEM;
		goto error_ret;
	}

	trig_info = kzalloc(sizeof(*trig_info), GFP_KERNEL);
	if (!trig_info) {
		ret = -ENOMEM;
		goto error_put_trigger;
	}

	atomic_set(&trig_info->count, 0);
	trig_info->irq = irq;
	iio_trigger_set_drvdata(trig, trig_info);
	trig->ops = &iio_interrupt_trigger_ops;
	trig->dev.groups = iio_interrupt_trigger_attr_groups;
	ret = iio_trigger_register(trig);
	if (ret)
		goto error_free_trig_info;

	/* Create a sysfs entry which the userspace may poll for irq events. */
	trig_info->poll = sysfs_get_dirent(trig->dev.kobj.sd, "count");
	if (trig_info->poll == NULL) {
		ret = -ENOENT;
		goto error_unregister_trig;
	}

	ret = request_irq(irq, iio_interrupt_trigger_poll,
			(irq_res->flags & IRQF_TRIGGER_MASK) | IRQF_SHARED,
			trig->name, trig);
	if (!ret) {
		platform_set_drvdata(pdev, trig);
		return 0;
	}

	sysfs_put(trig_info->poll);

/* First clean up the partly allocated trigger */
error_unregister_trig:
	iio_trigger_unregister(trig);
error_free_trig_info:
	kfree(trig_info);
error_put_trigger:
	iio_trigger_put(trig);
error_ret:
	return ret;
}

static int iio_interrupt_trigger_remove(struct platform_device *pdev)
{
	struct iio_trigger *trig;
	struct iio_interrupt_trigger_info *trig_info;

	trig = platform_get_drvdata(pdev);
	trig_info = iio_trigger_get_drvdata(trig);
	free_irq(trig_info->irq, trig);
	sysfs_put(trig_info->poll);
	iio_trigger_unregister(trig);
	kfree(trig_info);
	iio_trigger_put(trig);

	return 0;
}

#if defined(CONFIG_OF)
static const struct of_device_id iio_interrupt_trigger_of_match[] = {
	{ .compatible = "iio-interrupt-trigger" },
	{}
};
#endif

static struct platform_driver iio_interrupt_trigger_driver = {
	.probe = iio_interrupt_trigger_probe,
	.remove = iio_interrupt_trigger_remove,
	.driver = {
		.name           = "iio_interrupt_trigger",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(iio_interrupt_trigger_of_match)
	},
};

module_platform_driver(iio_interrupt_trigger_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Interrupt trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
