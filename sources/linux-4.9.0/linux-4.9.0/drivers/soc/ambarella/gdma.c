/*
 *
 * Author: Louis Sun <lysun@ambarella.com>
 *
 * History:
 *	2016/06/01 - [Ken He] Refactorisation
 *
 * Copyright (C) 2004-2018, Ambarella, Inc.
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
#include <linux/io.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <plat/iav_helper.h>
#include <plat/gdma.h>

struct ambagdma_device {
	void __iomem *regbase;
	struct device *dev;
	u32 irq;
};
static struct ambagdma_device *ambarella_gdma;

#define TRANSFER_2D_WIDTH		(1 << 12 )		/* 4096 */
#define MAX_TRANSFER_2D_HEIGHT		(1 << 11 )		/* 2048 */
#define MAX_TRANSFER_SIZE_2D_UNIT	(TRANSFER_2D_WIDTH * MAX_TRANSFER_2D_HEIGHT)	/* 8MB */

#define TRANSFER_1D_WIDTH		TRANSFER_2D_WIDTH
#define MAX_TRANSFER_SIZE_1D_UNIT	TRANSFER_1D_WIDTH

/* transfer 6 big blocks (although maximum is 8), because we may do another 1 small block and 1 line. total 8 Ops */
#define MAX_TRANSFER_SIZE_ONCE		(MAX_TRANSFER_SIZE_2D_UNIT * 6)	/* 48 MB */
#define MAX_OPS				8

static struct completion	transfer_completion;
static DEFINE_MUTEX(transfer_mutex);

/* handle 8MB at one time */
static inline int transfer_big_unit(u8 *dest_addr, u8 *src_addr, u32 size)
{
	int row_count;
	if (size > MAX_TRANSFER_SIZE_2D_UNIT) {
		printk("transfer_unit size %d bigger than %d \n",
			size, MAX_TRANSFER_SIZE_2D_UNIT);
		return -1;
	}

	row_count = size / TRANSFER_2D_WIDTH;

	/* copy rows by 2D copy */
	if (row_count > 0) {
		writel_relaxed((u32)(uintptr_t)src_addr, ambarella_gdma->regbase + GDMA_SRC_1_BASE_OFFSET);
		writel_relaxed(TRANSFER_2D_WIDTH, ambarella_gdma->regbase + GDMA_SRC_1_PITCH_OFFSET);
		writel_relaxed((u32)(uintptr_t)dest_addr, ambarella_gdma->regbase + GDMA_DST_BASE_OFFSET);
		writel_relaxed(TRANSFER_2D_WIDTH, ambarella_gdma->regbase + GDMA_DST_PITCH_OFFSET);
		writel_relaxed(TRANSFER_2D_WIDTH - 1, ambarella_gdma->regbase + GDMA_WIDTH_OFFSET);
		writel_relaxed(row_count - 1, ambarella_gdma->regbase + GDMA_HIGHT_OFFSET);
#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
		writel_relaxed(0x800, ambarella_gdma->regbase + GDMA_PIXELFORMAT_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_ALPHA_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_CLUT_BASE_OFFSET);
#endif

		/* start 2D copy */
		writel_relaxed(1, ambarella_gdma->regbase + GDMA_OPCODE_OFFSET);
	}
	return 0;

}

/* use 1D copy to copy max  4KB each time */
static inline int transfer_small_unit(u8 *dest_addr, u8 *src_addr, u32 size)
{
	if (size > TRANSFER_1D_WIDTH) {
		printk("transfer_unit size %d bigger than %d \n",
			size, TRANSFER_1D_WIDTH);
		return -1;
	}

	/* linear copy */
	writel_relaxed((u32)(uintptr_t)src_addr, ambarella_gdma->regbase + GDMA_SRC_1_BASE_OFFSET);
	writel_relaxed((u32)(uintptr_t)dest_addr, ambarella_gdma->regbase + GDMA_DST_BASE_OFFSET);
	writel_relaxed(size - 1, ambarella_gdma->regbase + GDMA_WIDTH_OFFSET);
#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
	writel_relaxed(0x800, ambarella_gdma->regbase + GDMA_PIXELFORMAT_OFFSET);
	writel_relaxed(0, ambarella_gdma->regbase + GDMA_ALPHA_OFFSET);
	writel_relaxed(0, ambarella_gdma->regbase + GDMA_CLUT_BASE_OFFSET);
#endif

	/* start linear copy */
	writel_relaxed(0, ambarella_gdma->regbase + GDMA_OPCODE_OFFSET);

	return 0;
}

/* this is async function, just fill dma registers and let it run*/
static inline int transfer_once(u8 *dest_addr, u8 *src_addr, u32 size)
{
	//total pending count must be no bigger than 8
	int big_count;
	int rows_count;
	int i;
	u32 transferred_bytes = 0;
	int remain_bytes ;

	if (size > MAX_TRANSFER_SIZE_ONCE)  {
		printk(" size too big %d for transfer once \n", size);
		return -1;
	}

	big_count = size/MAX_TRANSFER_SIZE_2D_UNIT;
	//big pages (each is 8MB)
	for (i = big_count ; i > 0; i--) {
		transfer_big_unit(dest_addr + transferred_bytes,
						src_addr  + transferred_bytes,
						MAX_TRANSFER_SIZE_2D_UNIT);
		transferred_bytes += MAX_TRANSFER_SIZE_2D_UNIT;
	}
	remain_bytes =  size - transferred_bytes;


	//transfer rows (align to TRANSFER_2D_WIDTH)
	rows_count = remain_bytes / TRANSFER_2D_WIDTH;
	if (rows_count > 0) {
		transfer_big_unit(dest_addr + transferred_bytes,
							src_addr  + transferred_bytes,
							TRANSFER_2D_WIDTH * rows_count);
		transferred_bytes += TRANSFER_2D_WIDTH * rows_count;
		remain_bytes =  size - transferred_bytes;
	}

	if (remain_bytes > 0) {
		transfer_small_unit(dest_addr + transferred_bytes,
						src_addr  + transferred_bytes, remain_bytes);
	}

	return 0;
}

/* this is synchronous function, will wait till transfer finishes */
int dma_memcpy(u8 *dest_addr, u8 *src_addr, u32 size)
{
	int remain_size = size;
	int transferred_size = 0;
	int current_transfer_size;

	if (size <= 0) {
		return -1;
	}

#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
	if (size & 0x1) {
		printk("Size must be even !\n");
		return -1;
	}
#endif

	mutex_lock(&transfer_mutex);

	ambcache_clean_range((void *)__phys_to_virt((unsigned long)src_addr), size);

	while (remain_size > 0)	{
		if (remain_size > MAX_TRANSFER_SIZE_ONCE) {
			remain_size -= MAX_TRANSFER_SIZE_ONCE;
			current_transfer_size = MAX_TRANSFER_SIZE_ONCE;
		} else {
			current_transfer_size = remain_size;
			remain_size = 0;
		}

		transfer_once(dest_addr + transferred_size,
			src_addr + transferred_size, current_transfer_size);
		wait_for_completion(&transfer_completion);
		transferred_size += current_transfer_size;
	}

	ambcache_inv_range((void *)__phys_to_virt((unsigned long)dest_addr), size);

	mutex_unlock(&transfer_mutex);

	return 0;
}
EXPORT_SYMBOL(dma_memcpy);

static inline int transfer_pitch_unit(u8 *dest_addr, u8 *src_addr,
					u16 src_pitch, u16 dest_pitch, u16 width, u16 height)
{

	if (height <= 0) {
		return -1;
	}

	/* copy rows by 2D copy */
	while (height > MAX_TRANSFER_2D_HEIGHT) {
		writel_relaxed((u32)(uintptr_t)src_addr, ambarella_gdma->regbase + GDMA_SRC_1_BASE_OFFSET);
		writel_relaxed(src_pitch, ambarella_gdma->regbase + GDMA_SRC_1_PITCH_OFFSET);
		writel_relaxed((u32)(uintptr_t)dest_addr, ambarella_gdma->regbase + GDMA_DST_BASE_OFFSET);
		writel_relaxed(dest_pitch, ambarella_gdma->regbase + GDMA_DST_PITCH_OFFSET);
		writel_relaxed(width - 1, ambarella_gdma->regbase + GDMA_WIDTH_OFFSET);
		writel_relaxed(MAX_TRANSFER_2D_HEIGHT - 1, ambarella_gdma->regbase + GDMA_HIGHT_OFFSET);
#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
		writel_relaxed(0x800, ambarella_gdma->regbase + GDMA_PIXELFORMAT_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_ALPHA_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_CLUT_BASE_OFFSET);
#endif

		/* start 2D copy */
		writel_relaxed(1, ambarella_gdma->regbase + GDMA_OPCODE_OFFSET);
		height = height - MAX_TRANSFER_2D_HEIGHT;
		src_addr = src_addr + src_pitch * MAX_TRANSFER_2D_HEIGHT;
		dest_addr = dest_addr + dest_pitch * MAX_TRANSFER_2D_HEIGHT;
	}

		writel_relaxed((u32)(uintptr_t)src_addr, ambarella_gdma->regbase + GDMA_SRC_1_BASE_OFFSET);
		writel_relaxed(src_pitch, ambarella_gdma->regbase + GDMA_SRC_1_PITCH_OFFSET);
		writel_relaxed((u32)(uintptr_t)dest_addr, ambarella_gdma->regbase + GDMA_DST_BASE_OFFSET);
		writel_relaxed(dest_pitch, ambarella_gdma->regbase + GDMA_DST_PITCH_OFFSET);
		writel_relaxed(width - 1, ambarella_gdma->regbase + GDMA_WIDTH_OFFSET);
		writel_relaxed(height - 1, ambarella_gdma->regbase + GDMA_HIGHT_OFFSET);
#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
		writel_relaxed(0x800, ambarella_gdma->regbase + GDMA_PIXELFORMAT_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_ALPHA_OFFSET);
		writel_relaxed(0, ambarella_gdma->regbase + GDMA_CLUT_BASE_OFFSET);
#endif

		/* start 2D copy */
		writel_relaxed(1, ambarella_gdma->regbase + GDMA_OPCODE_OFFSET);

	return 0;

}

/* this is synchronous function, will wait till transfer finishes  width =< 4096 */
int dma_pitch_memcpy(struct gdma_param *params)
{
	int size = params->src_pitch * params->height;

	if (size <= 0 || params->src_pitch <= 0 || params->dest_pitch <= 0
		|| params->width > TRANSFER_2D_WIDTH) {
		printk(" invalid value \n");
		return -1;
	}

#if (GDMA_SUPPORT_ALPHA_BLEND == 1)
	if (size & 0x1) {
		printk("Size must be even !\n");
		return -1;
	}
#endif

	mutex_lock(&transfer_mutex);
	if (!params->src_non_cached) {
		ambcache_clean_range((void *)params->src_virt_addr, size);
	}
	transfer_pitch_unit((u8 *)params->dest_addr, (u8 *)params->src_addr,
		params->src_pitch, params->dest_pitch, params->width, params->height);

	wait_for_completion(&transfer_completion);

	if (!params->dest_non_cached) {
		ambcache_inv_range((void *)params->dest_virt_addr, size);
	}
	mutex_unlock(&transfer_mutex);

	return 0;
}

EXPORT_SYMBOL(dma_pitch_memcpy);

/* wait till transmit completes */
static void wait_transmit_complete(struct ambagdma_device *amba_gdma)
{
	int pending_ops;
	pending_ops = readl_relaxed(amba_gdma->regbase + GDMA_PENDING_OPS_OFFSET);

	while(pending_ops!= 0) {
		mdelay(10);
	}
}

static irqreturn_t ambarella_gdma_irq(int irq, void *dev_data)
{
	struct ambagdma_device *amba_gdma = dev_data;
	int pending_ops;
	pending_ops = readl_relaxed(amba_gdma->regbase + GDMA_PENDING_OPS_OFFSET);

	if (pending_ops == 0) {
		/* if no following transfer */
		complete(&transfer_completion);
	} else {

	}
	return IRQ_HANDLED;
}

static int ambarella_gdma_probe(struct platform_device *pdev)
{
	struct ambagdma_device *ambagdma;
	struct resource *res;
	int ret = 0;

	ambagdma = devm_kzalloc(&pdev->dev, sizeof(struct ambagdma_device), GFP_KERNEL);
	if (!ambagdma) {
		dev_err(&pdev->dev, "Failed to allocate memory!\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource!\n");
		return -ENXIO;
	}

	ambagdma->regbase = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!ambagdma->regbase) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return -ENOMEM;
	}

	ambagdma->irq = platform_get_irq(pdev, 0);
	if (ambagdma->irq < 0) {
		dev_err(&pdev->dev, "Can not get irq !\n");
		return -ENXIO;
	}

	ret = devm_request_irq(&pdev->dev, ambagdma->irq,
			ambarella_gdma_irq, IRQF_TRIGGER_RISING,
			dev_name(&pdev->dev), ambagdma);
	if (ret < 0) {
		dev_err(&pdev->dev, "Can not request irq %d!\n", ambagdma->irq);
		return -ENXIO;
	}
	ambagdma->dev = &pdev->dev;
	ambarella_gdma = ambagdma;
	platform_set_drvdata(pdev, ambagdma);

	/* init completion */
	init_completion(&transfer_completion);

	dev_info(&pdev->dev, "Ambarella GDMA driver init\n");
	return 0;
}

static int ambarella_gdma_remove(struct platform_device *pdev)
{
	struct ambagdma_device *amba_gdma = platform_get_drvdata(pdev);
	wait_transmit_complete(amba_gdma);

	return 0;
}

static const struct of_device_id ambarella_gdma_dt_ids[] = {
	{.compatible = "ambarella,gdma"},
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_gdma_dt_ids);

static struct platform_driver ambarella_gdma_driver = {
	.driver = {
		.name = "ambarella-gdma",
		.of_match_table = ambarella_gdma_dt_ids,
	},
	.probe = ambarella_gdma_probe,
	.remove = ambarella_gdma_remove,
};
module_platform_driver(ambarella_gdma_driver);

MODULE_AUTHOR("Louis Sun <lysun@ambarella.com>");
MODULE_DESCRIPTION("GDMA driver on Ambarella S5");
MODULE_LICENSE("GPL");
