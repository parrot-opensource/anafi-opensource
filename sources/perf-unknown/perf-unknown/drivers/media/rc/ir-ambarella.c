/*
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * History:
 *	2016/07/03 - [Cao Rongrong] Create
 *
 * Copyright (C) 2014-2019, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <media/rc-core.h>
#include <plat/ir.h>

#define IR_AMBARELLA_NAME		"ambarella-ir"
#define IR_AMBARELLA_SAMPLE_RATE	200000

struct ambarella_ir_priv {
	int			irq;
	void __iomem		*base;
	struct device		*dev;
	struct rc_dev		*rdev;
	spinlock_t		lock;
	struct clk		*clk;
	u32			us_in_one_cycle;
	struct timer_list	timer;
	bool			pulse;
};

static int ambarella_ir_enable(struct ambarella_ir_priv *priv, bool on)
{
	u32 i, val;

	disable_irq(priv->irq);

	writel_relaxed(IR_CONTROL_RESET, priv->base + IR_CONTROL_OFFSET);

	if (!on)
		return 0;

	val = readl_relaxed(priv->base + IR_CONTROL_OFFSET);
	val |= IR_CONTROL_ENB | IR_CONTROL_INTLEV(16) | IR_CONTROL_INTENB;
	writel_relaxed(val, priv->base + IR_CONTROL_OFFSET);

	/* wait for IR controller stable */
	msleep(1);

	val = readl_relaxed(priv->base + IR_STATUS_OFFSET);
	for (i = 0; i < val; i++)
		readl_relaxed(priv->base + IR_DATA_OFFSET);

	enable_irq(priv->irq);

	return 0;
}

static int ambarella_ir_open(struct rc_dev *rdev)
{
	struct ambarella_ir_priv *priv = rdev->priv;

	return ambarella_ir_enable(priv, true);
}

static void ambarella_ir_close(struct rc_dev *rdev)
{
	struct ambarella_ir_priv *priv = rdev->priv;

	ambarella_ir_enable(priv, false);
}

static int ambarella_ir_read_data(struct ambarella_ir_priv *priv)
{
	u32 i, cnt, data;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	cnt = readl_relaxed(priv->base + IR_STATUS_OFFSET);
	if (cnt == 0)
		goto data_done;

	for (i = 0; i < cnt; i++) {
		DEFINE_IR_RAW_EVENT(ev);

		data = readl_relaxed(priv->base + IR_DATA_OFFSET);
		data *= priv->us_in_one_cycle;
		if (US_TO_NS(data) > IR_DEFAULT_TIMEOUT) {
			priv->pulse = true;
			continue;
		}

		ev.duration = US_TO_NS(data);
		ev.pulse = priv->pulse;
		ir_raw_event_store(priv->rdev, &ev);

		priv->pulse = !priv->pulse;
	}

data_done:
	spin_unlock_irqrestore(&priv->lock, flags);
	/* Empty software fifo */
	ir_raw_event_handle(priv->rdev);

	return cnt;
}

static void ambarella_ir_timer_timeout(unsigned long param)
{
	struct ambarella_ir_priv *priv = (struct ambarella_ir_priv *)param;

	if (ambarella_ir_read_data(priv) > 0)
		mod_timer(&priv->timer, jiffies + nsecs_to_jiffies(IR_DEFAULT_TIMEOUT));
	else
		ir_raw_event_set_idle(priv->rdev, true);
}

static irqreturn_t ambarella_ir_rx_interrupt(int irq, void *data)
{
	struct ambarella_ir_priv *priv = data;
	u32 ctrl_val;

	del_timer(&priv->timer);

	ctrl_val = readl_relaxed(priv->base + IR_CONTROL_OFFSET);
	if (ctrl_val & IR_CONTROL_FIFO_OV) {
		ir_raw_event_reset(priv->rdev);

		while (readl_relaxed(priv->base + IR_STATUS_OFFSET) > 0)
			readl_relaxed(priv->base + IR_DATA_OFFSET);

		ctrl_val = readl_relaxed(priv->base + IR_CONTROL_OFFSET);
		ctrl_val |= IR_CONTROL_FIFO_OV;
		writel_relaxed(ctrl_val, priv->base + IR_CONTROL_OFFSET);

		dev_err(priv->dev, "IR_CONTROL_FIFO_OV overflow\n");

		goto ambarella_ir_irq_exit;
	}

	ambarella_ir_read_data(priv);

	mod_timer(&priv->timer, jiffies + nsecs_to_jiffies(IR_DEFAULT_TIMEOUT));

ambarella_ir_irq_exit:
	/* Empty software fifo */
	ir_raw_event_handle(priv->rdev);

	ctrl_val = readl_relaxed(priv->base + IR_CONTROL_OFFSET);
	ctrl_val |= IR_CONTROL_LEVINT;
	writel_relaxed(ctrl_val, priv->base + IR_CONTROL_OFFSET);

	return IRQ_HANDLED;
}

static int ambarella_ir_probe(struct platform_device *pdev)
{
	struct rc_dev *rdev;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct ambarella_ir_priv *priv;
	struct device_node *node = pdev->dev.of_node;
	const char *map_name;
	int rval;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0) {
		dev_err(dev, "irq can not get\n");
		return priv->irq;
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "clk not found\n");
		return PTR_ERR(priv->clk);
	}

	clk_set_rate(priv->clk, IR_AMBARELLA_SAMPLE_RATE);
	priv->us_in_one_cycle = DIV_ROUND_UP(1000000, clk_get_rate(priv->clk));

	spin_lock_init(&priv->lock);
	setup_timer(&priv->timer, ambarella_ir_timer_timeout, (unsigned long)priv);

	rdev = rc_allocate_device();
	if (!rdev)
		return -ENOMEM;

	rdev->driver_type = RC_DRIVER_IR_RAW;
	rdev->allowed_protocols = RC_BIT_ALL;
	rdev->priv = priv;
	rdev->open = ambarella_ir_open;
	rdev->close = ambarella_ir_close;
	rdev->driver_name = IR_AMBARELLA_NAME;
	map_name = of_get_property(node, "linux,rc-map-name", NULL);
	rdev->map_name = map_name ?: RC_MAP_EMPTY;
	rdev->input_name = IR_AMBARELLA_NAME;
	rdev->input_phys = IR_AMBARELLA_NAME "/input0";
	rdev->input_id.bustype = BUS_HOST;
	rdev->input_id.vendor = 0x0001;
	rdev->input_id.product = 0x0001;
	rdev->input_id.version = 0x0100;
	rdev->rx_resolution = US_TO_NS(priv->us_in_one_cycle);
	rdev->timeout = IR_DEFAULT_TIMEOUT;

	rval = rc_register_device(rdev);
	if (rval < 0)
		goto err0;

	rval = devm_request_threaded_irq(dev, priv->irq,
					NULL, ambarella_ir_rx_interrupt,
					IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
					pdev->name, priv);
	if (rval < 0) {
		dev_err(dev, "IRQ %d register failed\n", priv->irq);
		goto err1;
	}

	priv->rdev = rdev;
	priv->dev = dev;
	platform_set_drvdata(pdev, priv);

	return rval;

err1:
	rc_unregister_device(rdev);
	rdev = NULL;
err0:
	rc_free_device(rdev);
	dev_err(dev, "Unable to register device (%d)\n", rval);
	return rval;
}

static int ambarella_ir_remove(struct platform_device *pdev)
{
	struct ambarella_ir_priv *priv = platform_get_drvdata(pdev);

	del_timer_sync(&priv->timer);
	rc_unregister_device(priv->rdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ambarella_ir_suspend(struct device *dev)
{
	struct ambarella_ir_priv *priv = dev_get_drvdata(dev);

	ambarella_ir_enable(priv, false);

	return 0;
}

static int ambarella_ir_resume(struct device *dev)
{
	struct ambarella_ir_priv *priv = dev_get_drvdata(dev);

	clk_set_rate(priv->clk, IR_AMBARELLA_SAMPLE_RATE);
	priv->us_in_one_cycle = DIV_ROUND_UP(1000000, clk_get_rate(priv->clk));

	ambarella_ir_enable(priv, true);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ambarella_ir_pm_ops, ambarella_ir_suspend,
			 ambarella_ir_resume);

static const struct of_device_id ambarella_ir_table[] = {
	{ .compatible = "ambarella,ir", },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_ir_table);

static struct platform_driver ambarella_ir_driver = {
	.driver = {
		.name = IR_AMBARELLA_NAME,
		.of_match_table = ambarella_ir_table,
		.pm     = &ambarella_ir_pm_ops,
	},
	.probe = ambarella_ir_probe,
	.remove = ambarella_ir_remove,
};

module_platform_driver(ambarella_ir_driver);

MODULE_DESCRIPTION("IR controller driver for ambarella platforms");
MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ambarella-ir");

