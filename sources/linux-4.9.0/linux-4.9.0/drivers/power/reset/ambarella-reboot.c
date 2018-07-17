/*
 * Ambarella SoC reset code
 *
 * Copyright (c) 2016 Ambarella Ltd.
 *
 * Author: Jorney < qtu@ambarella.com >
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <asm/proc-fns.h>
#include <plat/rct.h>

static struct regmap *reg_rct = NULL;

static int ambarella_restart_handler(struct notifier_block *this,
				unsigned long mode, void *cmd)
{

	local_irq_disable();
	local_fiq_disable();
	regmap_write(reg_rct, SOFT_OR_DLL_RESET_OFFSET, 0x6);
	regmap_write(reg_rct, SOFT_OR_DLL_RESET_OFFSET, 0x7);
	return NOTIFY_DONE;
}

static struct notifier_block ambarella_restart_nb = {
	.notifier_call = ambarella_restart_handler,
	.priority = 128,
};

static int ambarella_reboot_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int err;

	reg_rct = syscon_regmap_lookup_by_phandle(np, "amb,rct-regmap");
	if (IS_ERR(reg_rct)) {
		dev_err(&pdev->dev, "no rct regmap!\n");
		return PTR_ERR(reg_rct);
	}

	err = register_restart_handler(&ambarella_restart_nb);
	if (err)
		dev_err(&pdev->dev, "cannot register restart handler (err=%d)\n", err);

	return err;
}

static const struct of_device_id ambarella_reboot_of_match[] = {
	{ .compatible = "ambarella,reboot" },
	{}
};

static struct platform_driver ambarella_reboot_driver = {
	.probe = ambarella_reboot_probe,
	.driver = {
		.name = "ambarella-reboot",
		.of_match_table = ambarella_reboot_of_match,
	},
};
module_platform_driver(ambarella_reboot_driver);
