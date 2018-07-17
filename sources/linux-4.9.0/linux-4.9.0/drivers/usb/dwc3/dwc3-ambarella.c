/**
 * dwc3-ambarella.c - AMBARELLA Specific Glue layer
 *
 *
 * History:
 *	2016/02/22 - [Ken He] created file
 *
 * Copyright (C) 2016 by Ambarella, Inc.
 * http://www.ambarella.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/seq_file.h>
#include <linux/usb.h>
#include <linux/usb/phy.h>
#include <plat/rct.h>

//#include <linux/proc_fs.h>


/**
 * struct dwc3_ambarella - dwc3_ambarella driver private structure
 * @dev:		device pointer
 * @irq:		irq
 * @base:		device ioaddr for the glue registers
 * @regmap:		regmap pointer for getting syscfg
 * @syscfg_reg_off:	usb syscfg control offset
 * @dr_mode:		drd static host/device config
 * @rstc_pwrdn:		rest controller for powerdown signal
 * @rstc_rst:		reset controller for softreset signal
 */

struct dwc3_ambarella {
	struct device		*dev;
	struct regmap		*rct_reg;
	struct regmap		*scr_reg;

	u32			dma_status:1;

	//enum usb_dr_mode dr_mode;
	struct notifier_block	vbus_nb;
	struct notifier_block	id_nb;

	struct regulator	*vbus_reg;
};

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
#define USBP0_REF_USE_PAD		(1 << 24)
static void dwc3_ambarella_set_refclk(struct dwc3_ambarella *dwc3_amba,
				u32 ref_use_pad, u32 ref_clk_div2)
{
	regmap_update_bits(dwc3_amba->rct_reg, 0x634, USBP0_REF_USE_PAD,
				ref_use_pad << 24);
	msleep(1);
	regmap_update_bits(dwc3_amba->rct_reg, 0X2C4, 1 << 5, ref_clk_div2 << 5);
}
#endif

#define USB3_SOFT_RESET_MASK 0x4
static void dwc3_ambarella_reset(struct dwc3_ambarella	*dwc3_amba)
{
	regmap_update_bits(dwc3_amba->rct_reg, UDC_SOFT_RESET_OFFSET, USB3_SOFT_RESET_MASK, USB3_SOFT_RESET_MASK);
	msleep(1);
	regmap_update_bits(dwc3_amba->rct_reg, UDC_SOFT_RESET_OFFSET, USB3_SOFT_RESET_MASK, ~USB3_SOFT_RESET_MASK);
}

static int dwc3_ambarella_probe(struct platform_device *pdev)
{
	struct device_node	*node = pdev->dev.of_node;
	struct dwc3_ambarella	*dwc3_amba;
	struct device		*dev = &pdev->dev;
	int			ret;
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	struct usb_phy		*phy;
	u32			ref_use_pad, ref_clk_div2, poc;
#endif

	if (!node) {
		dev_err(dev, "device node not found\n");
		return -EINVAL;
	}

	dwc3_amba = devm_kzalloc(dev, sizeof(struct dwc3_ambarella), GFP_KERNEL);
	if (!dwc3_amba)
		return -ENOMEM;

	platform_set_drvdata(pdev, dwc3_amba);

	dwc3_amba->rct_reg = syscon_regmap_lookup_by_phandle(node, "amb,rct-regmap");
	if (IS_ERR(dwc3_amba->rct_reg)) {
		dev_err(dev, "no rct regmap!\n");
		return -ENODEV;
	}

	dwc3_amba->scr_reg = syscon_regmap_lookup_by_phandle(node, "amb,scr-regmap");
	if (IS_ERR(dwc3_amba->scr_reg)) {
		dev_err(dev, "no scr regmap!\n");
		return -ENODEV;
	}

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	/* dwc3 regs are hidden before enabling PHY. */
	/* Need to enable PHY before dwc3 driver runnuing. */
	/* Because dwc3 driver read its regs before calling usb_init_phy. */
	/* get the PHY device */
	phy = devm_usb_get_phy_by_phandle(dev, "amb,usbphy", 0);
	if (IS_ERR(phy)) {
		dev_err(dev, "Can't get USB PHY %ld\n", PTR_ERR(phy));
		return -ENXIO;
	}

	if (of_property_read_u32(dev->of_node, "amb,ref-use-pad", &ref_use_pad) < 0)
		ref_use_pad = 1;

	if (of_property_read_u32(dev->of_node, "amb,ref-clk-div2", &ref_clk_div2) < 0)
		ref_clk_div2 = 1;

	regmap_read(dwc3_amba->rct_reg, SYS_CONFIG_OFFSET, &poc);

	if (poc & SYS_CONFIG_USB_PHY_48M) {
		/* if using internal clock and clock is 48 MHz. */
		ref_use_pad = 0;
		ref_clk_div2 = 1;
	}

	usb_phy_init(phy);

	dwc3_ambarella_set_refclk(dwc3_amba, ref_use_pad, ref_clk_div2);
#endif

	dwc3_ambarella_reset(dwc3_amba);
	/* Select USB 3.0 */
	regmap_update_bits(dwc3_amba->scr_reg, 0x4c, 0x01, 0x01);

	dwc3_amba->dev	= dev;

#if 0
	dwc3_amba->irq	= irq;
	ret = devm_request_irq(dev, dwc3_amba->irq, dwc3_ambarella_interrupt,
			IRQF_TRIGGER_HIGH, dev_name(dev), dwc3_amba);
	if (ret) {
		dev_err(dev, "failed to request IRQ #%d --> %d\n",
				dwc3_amba->irq, ret);
		goto err1;
	}
#endif
	ret = of_platform_populate(node, NULL, NULL, dev);
	if (ret) {
		dev_err(dev, "failed to create dwc3 core\n");
	}

	return ret;
}

static int dwc3_ambarella_remove(struct platform_device *pdev)
{
	//struct dwc3_ambarella	*dwc3_amba = platform_get_drvdata(pdev);

	return 0;
}

static const struct of_device_id ambarella_dwc3_of_match[] = {
	{.compatible =	"ambarella,dwc3" },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_dwc3_of_match);

#ifdef CONFIG_PM_SLEEP
static int dwc3_ambarella_suspend(struct device *dev)
{
	//struct dwc3_ambarella	*dwc3_amba = dev_get_drvdata(dev);

	return 0;
}

static int dwc3_ambarella_resume(struct device *dev)
{
	//struct dwc3_ambarella	*dwc3_amba = dev_get_drvdata(dev);

	return 0;

}

static const struct dev_pm_ops dwc3_ambarella_dev_pm_ops = {

	SET_SYSTEM_SLEEP_PM_OPS(dwc3_ambarella_suspend, dwc3_ambarella_resume)
};

#define DEV_PM_OPS	(&dwc3_ambarella_dev_pm_ops)
#else
#define DEV_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

static struct platform_driver dwc3_ambarella_driver = {
	.probe		= dwc3_ambarella_probe,
	.remove		= dwc3_ambarella_remove,
	.driver		= {
		.name	= "ambarella-dwc3",
		.of_match_table	= ambarella_dwc3_of_match,
		.pm	= DEV_PM_OPS,
	},
};

module_platform_driver(dwc3_ambarella_driver);

MODULE_ALIAS("platform:ambarella-dwc3");
MODULE_AUTHOR("Ken He <jianhe@ambarella.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DesignWare USB3 AMBARELLA Glue Layer");
