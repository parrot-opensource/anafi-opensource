/*
 * drivers/rtc/ambarella_rtc.c
 *
 * History:
 *	2008/04/01 - [Cao Rongrong] Support pause and resume
 *	2009/01/22 - [Anthony Ginger] Port to 2.6.28
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/clk.h>
#include <asm/uaccess.h>
#include <plat/rtc.h>

#define AMBRTC_TIME		0
#define AMBRTC_ALARM		1

struct ambarella_rtc {
	struct rtc_device	*rtc;
	void __iomem	*base;
	struct device	*dev;
	int	lost_power;
	int	irq;
};

static inline void ambrtc_strobe_set(struct ambarella_rtc *ambrtc)
{
	writel_relaxed(0x01, ambrtc->base + PWC_RESET_OFFSET);
	msleep(3);
	writel_relaxed(0x00, ambrtc->base + PWC_RESET_OFFSET);
}

#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
static void ambrtc_registers_fflush(struct ambarella_rtc *ambrtc)
{
	unsigned int time, alarm, status;

	writel_relaxed(0x80, ambrtc->base + PWC_POS0_OFFSET);
	writel_relaxed(0x80, ambrtc->base + PWC_POS1_OFFSET);
	writel_relaxed(0x80, ambrtc->base + PWC_POS2_OFFSET);

	time = readl_relaxed(ambrtc->base + RTC_CURT_READ_OFFSET);
	writel_relaxed(time, ambrtc->base + RTC_CURT_WRITE_OFFSET);

	alarm = readl_relaxed(ambrtc->base + RTC_ALAT_READ_OFFSET);
	writel_relaxed(alarm, ambrtc->base + RTC_ALAT_WRITE_OFFSET);

	status = readl_relaxed(ambrtc->base + PWC_REG_STA_OFFSET);
	if (ambrtc->lost_power)
		status |= PWC_STA_LOSS_MASK;

	status &= ~PWC_STA_SR_MASK;
	status &= ~PWC_STA_ALARM_MASK;

	writel_relaxed(status, ambrtc->base + PWC_SET_STATUS_OFFSET);
}
#endif

static int ambrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct ambarella_rtc *ambrtc;
	u32 time_sec;

	ambrtc = dev_get_drvdata(dev);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	/* Synchronize to ThreadX system time (TAI time spec).*/
	time_sec = readl_relaxed(ambrtc->base + RTC_CURT_READ_OFFSET) + 10;
#else
	time_sec = readl_relaxed(ambrtc->base + RTC_CURT_READ_OFFSET);
#endif

	rtc_time_to_tm(time_sec, tm);

	return 0;
}

static int ambrtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct ambarella_rtc *ambrtc;

	ambrtc = dev_get_drvdata(dev);

#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
	ambrtc_registers_fflush(ambrtc);
	writel_relaxed(secs, ambrtc->base + RTC_CURT_WRITE_OFFSET);

	writel_relaxed(0x01, ambrtc->base + PWC_BC_OFFSET);
	ambrtc_strobe_set(ambrtc);
	writel_relaxed(0x00, ambrtc->base + PWC_BC_OFFSET);
#else
	dev_warn(ambrtc->dev, "%s is not supported in dual-OSes!\n", __func__);
#endif

	return 0;
}

static int ambrtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct ambarella_rtc *ambrtc;
	u32 alarm_sec, time_sec, status;

	ambrtc = dev_get_drvdata(dev);


#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	/* Synchronize to ThreadX system time (TAI time spec).*/
	alarm_sec = readl_relaxed(ambrtc->base + RTC_ALAT_READ_OFFSET) + 10;
#else
	alarm_sec = readl_relaxed(ambrtc->base + RTC_ALAT_READ_OFFSET);
#endif
	rtc_time_to_tm(alarm_sec, &alrm->time);
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	/* Synchronize to ThreadX system time (TAI time spec).*/
	time_sec = readl_relaxed(ambrtc->base + RTC_CURT_READ_OFFSET) + 10;
#else
	time_sec = readl_relaxed(ambrtc->base + RTC_CURT_READ_OFFSET);
#endif

	/* assert alarm is enabled if alrm time is after current time */
	alrm->enabled = alarm_sec > time_sec;

	status = readl_relaxed(ambrtc->base + RTC_STATUS_OFFSET);
	alrm->pending = !!(status & RTC_STATUS_ALA_WK);

	return 0;
}

static int ambrtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
	struct ambarella_rtc *ambrtc;
	unsigned long alarm_sec;
	int status;

	ambrtc = dev_get_drvdata(dev);

	rtc_tm_to_time(&alrm->time, &alarm_sec);

	ambrtc_registers_fflush(ambrtc);

	status = readl_relaxed(ambrtc->base + PWC_SET_STATUS_OFFSET);
	status |= PWC_STA_ALARM_MASK;

	writel_relaxed(alarm_sec, ambrtc->base + RTC_ALAT_WRITE_OFFSET);

	ambrtc_strobe_set(ambrtc);
#else
	struct ambarella_rtc *ambrtc;

	ambrtc = dev_get_drvdata(dev);
	dev_warn(ambrtc->dev, "%s is not supported in dual-OSes!\n", __func__);
#endif

	return 0;
}

static int ambrtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	return 0;
}

static irqreturn_t ambrtc_alarm_irq(int irq, void *dev_id)
{
	struct ambarella_rtc *ambrtc = (struct ambarella_rtc *)dev_id;

	if(ambrtc->rtc)
		rtc_update_irq(ambrtc->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int ambrtc_ioctl(struct device *dev, unsigned int cmd,
			     unsigned long arg)
{
	struct ambarella_rtc *ambrtc;
	int lbat, rval = 0;

	ambrtc = dev_get_drvdata(dev);

	switch (cmd) {
	case RTC_VL_READ:
		lbat = !!readl_relaxed(ambrtc->base + PWC_LBAT_OFFSET);
		rval = put_user(lbat, (int __user *)arg);
		break;
	default:
		rval = -ENOIOCTLCMD;
		break;
	}

	return rval;
}

static const struct rtc_class_ops ambarella_rtc_ops = {
	.ioctl		= ambrtc_ioctl,
	.read_time	= ambrtc_read_time,
	.set_mmss	= ambrtc_set_mmss,
	.read_alarm	= ambrtc_read_alarm,
	.set_alarm	= ambrtc_set_alarm,
	.alarm_irq_enable = ambrtc_alarm_irq_enable,
};

static int ambrtc_probe(struct platform_device *pdev)
{
	struct ambarella_rtc *ambrtc;
	struct resource *res;
	int ret;

	ambrtc = devm_kzalloc(&pdev->dev, sizeof(*ambrtc), GFP_KERNEL);
	if (!ambrtc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ambrtc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ambrtc->base))
		return PTR_ERR(ambrtc->base);

	ambrtc->irq = platform_get_irq(pdev, 0);
	if (ambrtc->irq < 0) {
		ambrtc->irq = 0;
	} else {
		ret = devm_request_irq(&pdev->dev, ambrtc->irq, ambrtc_alarm_irq,
				IRQF_SHARED, pdev->name, ambrtc);
		if (ret) {
			dev_err(&pdev->dev, "interrupt not available.\n");
			return ret;
		}
	}

	ambrtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, ambrtc);

	ambrtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				     &ambarella_rtc_ops, THIS_MODULE);
	if (IS_ERR(ambrtc->rtc)) {
		dev_err(&pdev->dev, "devm_rtc_device_register fail.\n");
		return PTR_ERR(ambrtc->rtc);
	}
	ambrtc->lost_power =
		!(readl_relaxed(ambrtc->base + PWC_REG_STA_OFFSET) & PWC_STA_LOSS_MASK);

	if (ambrtc->lost_power) {
		dev_warn(ambrtc->dev, "Warning: RTC lost power.....\n");
		ambrtc_set_mmss(ambrtc->dev, 0);
		ambrtc->lost_power = 0;
	}

	ambrtc->rtc->uie_unsupported = 1;

	device_init_wakeup(&pdev->dev, true);

	return 0;
}

static int ambrtc_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, false);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id ambarella_rtc_dt_ids[] = {
	{.compatible = "ambarella,rtc", },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_rtc_dt_ids);

#ifdef CONFIG_PM_SLEEP
static int ambarella_rtc_suspend(struct device *dev)
{
	struct ambarella_rtc *ambrtc = dev_get_drvdata(dev);

	if (ambrtc->irq & device_may_wakeup(dev))
		enable_irq_wake(ambrtc->irq);

	return 0;
}

static int ambarella_rtc_resume(struct device *dev)
{
	struct ambarella_rtc *ambrtc = dev_get_drvdata(dev);

	if (ambrtc->irq & device_may_wakeup(dev))
		disable_irq_wake(ambrtc->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ambarella_rtc_pm_ops, ambarella_rtc_suspend, ambarella_rtc_resume);

static struct platform_driver ambarella_rtc_driver = {
	.probe		= ambrtc_probe,
	.remove		= ambrtc_remove,
	.driver		= {
		.name	= "ambarella-rtc",
		.owner	= THIS_MODULE,
		.of_match_table = ambarella_rtc_dt_ids,
		.pm	= &ambarella_rtc_pm_ops,
	},
};

module_platform_driver(ambarella_rtc_driver);

MODULE_DESCRIPTION("Ambarella Onchip RTC Driver.v200");
MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_LICENSE("GPL");

