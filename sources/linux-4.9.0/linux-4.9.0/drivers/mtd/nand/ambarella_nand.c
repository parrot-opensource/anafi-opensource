/*
 * drivers/mtd/ambarella_nand.c
 *
 * History:
 *	2008/04/11 - [Cao Rongrong & Chien-Yang Chen] created file
 *	2009/01/04 - [Anthony Ginger] Port to 2.6.28
 *	2012/05/23 - [Ken He] Add the dma engine driver method support
 *	2012/07/03 - [Ken He] use the individual FDMA for nand
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
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/bitrev.h>
#include <linux/bch.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>
#include <plat/rct.h>
#include <plat/dma.h>
#include <plat/nand.h>
#include <plat/fio.h>
#include <plat/event.h>

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
#include <linux/aipc/ipc_mutex.h>
#endif

#define AMBARELLA_NAND_DMA_BUFFER_SIZE	4096


struct ambarella_nand_info {
	struct nand_chip		chip;
	struct nand_hw_control		controller;

	struct device			*dev;
	wait_queue_head_t		wq;

	void __iomem			*regbase;
	void __iomem			*fdmaregbase;
	struct regmap			*reg_rct;
	u32				dmabase;
	/* dma irq for transferring data between Nand and FIFO */
	int				dma_irq;
	int				cmd_irq;
	/* fdma irq for transferring data between FIFO and Dram */
	int				fdma_irq;
	u32				ecc_bits;
	/* if or not support to read id in 5 cycles */
	bool				id_cycles_5;
	bool				soft_ecc;
	bool				nand_wp;

	/* used for software BCH */
	struct bch_control		*bch;
	u32				*errloc;
	u8				*bch_data;
	u8				read_ecc_rev[13];
	u8				calc_ecc_rev[13];
	u8				soft_bch_extra_size;

	dma_addr_t			dmaaddr;
	u8				*dmabuf;
	int				dma_bufpos;
	u32				dma_status;
	u32				fio_dma_sta;
	u32				fio_ecc_sta;
	atomic_t			irq_flag;

	/* saved column/page_addr during CMD_SEQIN */
	int				seqin_column;
	int				seqin_page_addr;

	/* Operation parameters for nand controller register */
	int				err_code;
	u32				cmd;
	u32				control_reg;
	u32				addr_hi;
	u32				addr;
	u32				dst;
	dma_addr_t			buf_phys;
	dma_addr_t			spare_buf_phys;
	u32				len;
	u32				slen;
	u32				area;
	u32				ecc;
	u32				timing[6];

	struct notifier_block		system_event;
	struct semaphore		system_event_sem;

};

/* ==========================================================================*/
#define NAND_TIMING_RSHIFT24BIT(x)	(((x) & 0xff000000) >> 24)
#define NAND_TIMING_RSHIFT16BIT(x)	(((x) & 0x00ff0000) >> 16)
#define NAND_TIMING_RSHIFT8BIT(x)	(((x) & 0x0000ff00) >> 8)
#define NAND_TIMING_RSHIFT0BIT(x)	(((x) & 0x000000ff) >> 0)

#define NAND_TIMING_LSHIFT24BIT(x)	((x) << 24)
#define NAND_TIMING_LSHIFT16BIT(x)	((x) << 16)
#define NAND_TIMING_LSHIFT8BIT(x)	((x) << 8)
#define NAND_TIMING_LSHIFT0BIT(x)	((x) << 0)

#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
static int nand_timing_calc(u32 clk, int minmax, int val)
{
	u32 x;
	int n,r;

	x = val * clk;
	n = x / 1000;
	r = x % 1000;

	if (r != 0)
		n++;

	if (minmax)
		n--;
	return n < 1 ? 1 : n;
}
#endif

static int amb_ecc1_ooblayout_ecc_sp(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 8;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int amb_ecc1_ooblayout_free_sp(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section > chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = section * 11;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc1_sp_ooblayout_ops = {
	.ecc = amb_ecc1_ooblayout_ecc_sp,
	.free = amb_ecc1_ooblayout_free_sp,
};

static int amb_ecc1_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	if (section % 2) {
		oobregion->offset = (section * 16) + 13;
		oobregion->length = 3;
	} else {
		oobregion->offset = (section * 16) + 1;
		oobregion->length = 7;
	}
	return 0;
}

static int amb_ecc1_ooblayout_free_lp(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= ((chip->ecc.steps) << 1))
		return -ERANGE;

	oobregion->offset = section * 11;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc1_lp_ooblayout_ops = {
	.ecc = amb_ecc1_ooblayout_ecc_lp,
	.free = amb_ecc1_ooblayout_free_lp,
};

static int amb_ecc6_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 6;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int amb_ecc6_ooblayout_free_lp(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 16) + 1;
	oobregion->length = 5;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc6_lp_ooblayout_ops = {
	.ecc = amb_ecc6_ooblayout_ecc_lp,
	.free = amb_ecc6_ooblayout_free_lp,
};

static int amb_ecc8_ooblayout_ecc_lp(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 32) + 19;
	oobregion->length = chip->ecc.bytes;

	return 0;
}

static int amb_ecc8_ooblayout_free_lp(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);

	if (section >= chip->ecc.steps)
		return -ERANGE;

	oobregion->offset = (section * 32) + 2;;
	oobregion->length = 17;

	return 0;
}

static const struct mtd_ooblayout_ops amb_ecc8_lp_ooblayout_ops = {
	.ecc = amb_ecc8_ooblayout_ecc_lp,
	.free = amb_ecc8_ooblayout_free_lp,
};

static inline int nand_amb_is_hw_bch(struct ambarella_nand_info *nand_info)
{
	return !nand_info->soft_ecc && nand_info->ecc_bits > 1;
}

static inline int nand_amb_is_sw_bch(struct ambarella_nand_info *nand_info)
{
	return nand_info->soft_ecc && nand_info->ecc_bits > 1;
}

static inline void ambarella_fio_rct_reset(struct ambarella_nand_info *nand_info)
{
	regmap_write(nand_info->reg_rct, FIO_RESET_OFFSET, FIO_RESET_FIO_RST);
	msleep(1);
	regmap_write(nand_info->reg_rct, FIO_RESET_OFFSET, 0);
	msleep(1);
}

static void nand_amb_corrected_recovery(struct ambarella_nand_info *nand_info)
{
	u32 fio_ctr_reg, fio_dmactr_reg;

	/* FIO reset will just reset FIO registers, but will not affect
	 * Nand controller. */
	fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);
	fio_dmactr_reg = readl_relaxed(nand_info->regbase + FIO_DMACTR_OFFSET);
	ambarella_fio_rct_reset(nand_info);
	writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);
	writel_relaxed(fio_dmactr_reg, nand_info->regbase + FIO_DMACTR_OFFSET);
}

static void nand_amb_enable_dsm(struct ambarella_nand_info *nand_info)
{
	u32 fio_dsm_ctr = 0, fio_ctr_reg = 0, dma_dsm_ctr = 0;
	fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);

	fio_dsm_ctr |= (FIO_DSM_EN | FIO_DSM_MAJP_2KB);
	dma_dsm_ctr |= (DMA_DSM_EN | DMA_DSM_MAJP_2KB);
	fio_ctr_reg |= FIO_CTR_RS;
	fio_ctr_reg &= ~(FIO_CTR_CO | FIO_CTR_SE | FIO_CTR_ECC_6BIT
					| FIO_CTR_ECC_8BIT);

	if (nand_info->ecc_bits == 6) {
		fio_dsm_ctr |= FIO_DSM_SPJP_64B;
		dma_dsm_ctr |= DMA_DSM_SPJP_64B;
	} else {
		u32 nand_ext_ctr_reg = 0;
		fio_dsm_ctr |= FIO_DSM_SPJP_128B;
		dma_dsm_ctr |= DMA_DSM_SPJP_128B;

		nand_ext_ctr_reg = readl_relaxed(nand_info->regbase +
				FLASH_EX_CTR_OFFSET);
		nand_ext_ctr_reg |= NAND_EXT_CTR_SP_2X;
		writel_relaxed(nand_ext_ctr_reg, nand_info->regbase + FLASH_EX_CTR_OFFSET);
	}

	writel_relaxed(fio_ctr_reg | FIO_CTR_RR, nand_info->regbase + FIO_CTR_OFFSET);
	writel_relaxed(fio_dsm_ctr, nand_info->regbase + FIO_DSM_CTR_OFFSET);
	writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);
	writel_relaxed(dma_dsm_ctr, nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET);
}

static void nand_amb_enable_bch(struct ambarella_nand_info *nand_info)
{
	u32 fio_dsm_ctr = 0, fio_ctr_reg = 0, dma_dsm_ctr = 0;
	fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);

	fio_dsm_ctr |= (FIO_DSM_EN | FIO_DSM_MAJP_2KB);
	dma_dsm_ctr |= (DMA_DSM_EN | DMA_DSM_MAJP_2KB);
	fio_ctr_reg |= (FIO_CTR_RS | FIO_CTR_CO | FIO_CTR_SKIP_BLANK);

	if (nand_info->ecc_bits == 6) {
		fio_dsm_ctr |= FIO_DSM_SPJP_64B;
		dma_dsm_ctr |= DMA_DSM_SPJP_64B;
		fio_ctr_reg |= FIO_CTR_ECC_6BIT;
	} else {
		u32 nand_ext_ctr_reg = 0;
		fio_dsm_ctr |= FIO_DSM_SPJP_128B;
		dma_dsm_ctr |= DMA_DSM_SPJP_128B;
		fio_ctr_reg |= FIO_CTR_ECC_8BIT;
		nand_ext_ctr_reg = readl_relaxed(nand_info->regbase +
				FLASH_EX_CTR_OFFSET);
		nand_ext_ctr_reg |= NAND_EXT_CTR_SP_2X;
		writel_relaxed(nand_ext_ctr_reg, nand_info->regbase + FLASH_EX_CTR_OFFSET);
	}

	writel_relaxed(fio_ctr_reg | FIO_CTR_RR, nand_info->regbase + FIO_CTR_OFFSET);
	writel_relaxed(fio_dsm_ctr, nand_info->regbase + FIO_DSM_CTR_OFFSET);
	writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);
	writel_relaxed(dma_dsm_ctr, nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET);
}
#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
static void nand_amb_disable_bch(struct ambarella_nand_info *nand_info)
{
	u32 fio_ctr_reg = 0;

	fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);
	/* Setup FIO Dual Space Mode Control Register */
	fio_ctr_reg |= FIO_CTR_RS;
	fio_ctr_reg &= ~(FIO_CTR_CO |
			 FIO_CTR_ECC_6BIT |
			 FIO_CTR_ECC_8BIT);

	if (nand_info->ecc_bits == 8) {
		u32 nand_ext_ctr_reg = 0;
		nand_ext_ctr_reg = readl_relaxed(nand_info->regbase +
					FLASH_EX_CTR_OFFSET);
		nand_ext_ctr_reg &= ~NAND_EXT_CTR_SP_2X;
		writel_relaxed(nand_ext_ctr_reg, nand_info->regbase + FLASH_EX_CTR_OFFSET);
	}
	writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);

	writel_relaxed(0, nand_info->regbase + FIO_DSM_CTR_OFFSET);
	writel_relaxed(0, nand_info->fdmaregbase + FDMA_DSM_CTR_OFFSET);
}
#endif

static int count_zero_bits(u8 *buf, int size, int max_bits)
{
	int i, zero_bits = 0;

	for (i = 0; i < size; i++) {
		zero_bits += hweight8(~buf[i]);
		if (zero_bits > max_bits)
			break;
	}
	return zero_bits;
}

static int nand_bch_check_blank_page(struct ambarella_nand_info *nand_info)
{
	struct nand_chip *chip = &nand_info->chip;
	struct mtd_info	*mtd = nand_to_mtd(chip);
	int eccsteps = chip->ecc.steps;
	int zeroflip = 0;
	int oob_subset;
	int zero_bits = 0;
	u32 i;
	u8 *bufpos;
	u8 *bsp;

	bufpos = nand_info->dmabuf;
	bsp = nand_info->dmabuf + mtd->writesize;
	oob_subset = mtd->oobsize / eccsteps;

	for (i = 0; i < eccsteps; i++) {
		zero_bits = count_zero_bits(bufpos, chip->ecc.size,
								chip->ecc.strength);
		if (zero_bits > chip->ecc.strength)
			return -1;

		if (zero_bits)
			zeroflip = 1;

		zero_bits += count_zero_bits(bsp, oob_subset,
								chip->ecc.strength);
		if (zero_bits > chip->ecc.strength)
			return -1;

		bufpos += chip->ecc.size;
		bsp += oob_subset;
	}

	if (zeroflip)
		memset(nand_info->dmabuf, 0xff, mtd->writesize);

	return 0;
}

static void amb_nand_set_timing(struct ambarella_nand_info *nand_info)
{
#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
	u8 tcls, tals, tcs, tds;
	u8 tclh, talh, tch, tdh;
	u8 twp, twh, twb, trr;
	u8 trp, treh, trb, tceh;
	u8 trdelay, tclr, twhr, tir;
	u8 tww, trhz, tar;
	u32 i, poc, clk_div2, t, clk, val;

	for (i = 0; i < ARRAY_SIZE(nand_info->timing); i++) {
		if (nand_info->timing[i] != 0x0)
			break;
	}
	/* if the timing is not setup by Amboot, we leave the timing unchanged */
	if (i == ARRAY_SIZE(nand_info->timing))
		return;

	regmap_read(nand_info->reg_rct, SYS_CONFIG_OFFSET, &poc);
	clk_div2 = POC_GCLK_CORE_DIV2_MASK ? !(poc & POC_GCLK_CORE_DIV2_MASK) : 0;

	clk = (clk_get_rate(clk_get(nand_info->dev, NULL)) / 1000000);
	clk >>= clk_div2;

	/* timing 0 */
	t = nand_info->timing[0];
	tcls = NAND_TIMING_RSHIFT24BIT(t);
	tals = NAND_TIMING_RSHIFT16BIT(t);
	tcs = NAND_TIMING_RSHIFT8BIT(t);
	tds = NAND_TIMING_RSHIFT0BIT(t);

	tcls = nand_timing_calc(clk, 0, tcls);
	tals = nand_timing_calc(clk, 0, tals);
	tcs = nand_timing_calc(clk, 0, tcs);
	tds = nand_timing_calc(clk, 0, tds);

	val = NAND_TIMING_LSHIFT24BIT(tcls) |
		NAND_TIMING_LSHIFT16BIT(tals) |
		NAND_TIMING_LSHIFT8BIT(tcs) |
		NAND_TIMING_LSHIFT0BIT(tds);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20202020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM0_OFFSET);

	/* timing 1 */
	t = nand_info->timing[1];
	tclh = NAND_TIMING_RSHIFT24BIT(t);
	talh = NAND_TIMING_RSHIFT16BIT(t);
	tch = NAND_TIMING_RSHIFT8BIT(t);
	tdh = NAND_TIMING_RSHIFT0BIT(t);

	tclh = nand_timing_calc(clk, 0, tclh);
	talh = nand_timing_calc(clk, 0, talh);
	tch = nand_timing_calc(clk, 0, tch);
	tdh = nand_timing_calc(clk, 0, tdh);

	val = NAND_TIMING_LSHIFT24BIT(tclh) |
		NAND_TIMING_LSHIFT16BIT(talh) |
		NAND_TIMING_LSHIFT8BIT(tch) |
		NAND_TIMING_LSHIFT0BIT(tdh);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20202020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM1_OFFSET);

	/* timing 2 */
	t = nand_info->timing[2];
	twp = NAND_TIMING_RSHIFT24BIT(t);
	twh = NAND_TIMING_RSHIFT16BIT(t);
	twb = NAND_TIMING_RSHIFT8BIT(t);
	trr = NAND_TIMING_RSHIFT0BIT(t);

	twp = nand_timing_calc(clk, 0, twp);
	twh = nand_timing_calc(clk, 0, twh);
	twb = nand_timing_calc(clk, 1, twb);
	trr = nand_timing_calc(clk, 0, trr);

	val = NAND_TIMING_LSHIFT24BIT(twp) |
		NAND_TIMING_LSHIFT16BIT(twh) |
		NAND_TIMING_LSHIFT8BIT(twb) |
		NAND_TIMING_LSHIFT0BIT(trr);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20204020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM2_OFFSET);

	/* timing 3 */
	t = nand_info->timing[3];
	trp = NAND_TIMING_RSHIFT24BIT(t);
	treh = NAND_TIMING_RSHIFT16BIT(t);
	trb = NAND_TIMING_RSHIFT8BIT(t);
	tceh = NAND_TIMING_RSHIFT0BIT(t);

	trp = nand_timing_calc(clk, 0, trp);
	treh = nand_timing_calc(clk, 0, treh);
	trb = nand_timing_calc(clk, 1, trb);
	tceh = nand_timing_calc(clk, 1, tceh);

	val = NAND_TIMING_LSHIFT24BIT(trp) |
		NAND_TIMING_LSHIFT16BIT(treh) |
		NAND_TIMING_LSHIFT8BIT(trb) |
		NAND_TIMING_LSHIFT0BIT(tceh);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20202020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM3_OFFSET);

	/* timing 4 */
	t = nand_info->timing[4];
	trdelay = NAND_TIMING_RSHIFT24BIT(t);
	tclr = NAND_TIMING_RSHIFT16BIT(t);
	twhr = NAND_TIMING_RSHIFT8BIT(t);
	tir = NAND_TIMING_RSHIFT0BIT(t);

	trdelay = trp + treh;
	tclr = nand_timing_calc(clk, 0, tclr);
	twhr = nand_timing_calc(clk, 0, twhr);
	tir = nand_timing_calc(clk, 0, tir);

	val = NAND_TIMING_LSHIFT24BIT(trdelay) |
		NAND_TIMING_LSHIFT16BIT(tclr) |
		NAND_TIMING_LSHIFT8BIT(twhr) |
		NAND_TIMING_LSHIFT0BIT(tir);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20202020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM4_OFFSET);

	/* timing 5 */
	t = nand_info->timing[5];
	tww = NAND_TIMING_RSHIFT16BIT(t);
	trhz = NAND_TIMING_RSHIFT8BIT(t);
	tar = NAND_TIMING_RSHIFT0BIT(t);

	tww = nand_timing_calc(clk, 0, tww);
	trhz = nand_timing_calc(clk, 1, trhz);
	tar = nand_timing_calc(clk, 0, tar);


	val = NAND_TIMING_LSHIFT16BIT(tww) |
		NAND_TIMING_LSHIFT8BIT(trhz) |
		NAND_TIMING_LSHIFT0BIT(tar);

	/* use default timing if gclk_core <= 96MHz */
	if (clk <= 96)
		val = 0x20202020;

	writel_relaxed(val, nand_info->regbase + FLASH_TIM5_OFFSET);
#endif
}

static int ambarella_nand_system_event(struct notifier_block *nb,
	unsigned long val, void *data)
{
	int					errorCode = NOTIFY_OK;
	struct ambarella_nand_info		*nand_info;

	nand_info = container_of(nb, struct ambarella_nand_info, system_event);

	switch (val) {
	case AMBA_EVENT_PRE_CPUFREQ:
		pr_debug("%s: Pre Change\n", __func__);
		down(&nand_info->system_event_sem);
		break;

	case AMBA_EVENT_POST_CPUFREQ:
		pr_debug("%s: Post Change\n", __func__);
		amb_nand_set_timing(nand_info);
		up(&nand_info->system_event_sem);
		break;

	default:
		break;
	}

	return errorCode;
}

static irqreturn_t nand_fiocmd_isr_handler(int irq, void *dev_id)
{
	irqreturn_t				rval = IRQ_NONE;
	struct ambarella_nand_info		*nand_info;
	u32					val;

	nand_info = (struct ambarella_nand_info *)dev_id;

	val = readl_relaxed(nand_info->regbase + FIO_STA_OFFSET);

	if (val & FIO_STA_FI) {
		writel_relaxed(0x0, nand_info->regbase + FLASH_INT_OFFSET);
		atomic_and(~0x1, &nand_info->irq_flag);
		wake_up(&nand_info->wq);

		rval = IRQ_HANDLED;
	}

	return rval;
}

/* this dma is used to transfer data between Nand and FIO FIFO. */
static irqreturn_t nand_fiodma_isr_handler(int irq, void *dev_id)
{
	struct ambarella_nand_info		*nand_info;
	u32					val, fio_dma_sta;

	nand_info = (struct ambarella_nand_info *)dev_id;

	val = readl_relaxed(nand_info->regbase + FIO_DMACTR_OFFSET);

	if ((val & (FIO_DMACTR_SD | FIO_DMACTR_CF |
		FIO_DMACTR_XD | FIO_DMACTR_FL)) ==  FIO_DMACTR_FL) {
		fio_dma_sta = readl_relaxed(nand_info->regbase + FIO_DMASTA_OFFSET);
		/* dummy IRQ by S2 chip */
		if (fio_dma_sta == 0x0)
			return IRQ_HANDLED;

		nand_info->fio_dma_sta = fio_dma_sta;

		writel_relaxed(0x0, nand_info->regbase + FIO_DMASTA_OFFSET);

		if (nand_amb_is_hw_bch(nand_info)) {
			nand_info->fio_ecc_sta =
				readl_relaxed(nand_info->regbase + FIO_ECC_RPT_STA_OFFSET);
			writel_relaxed(0x0, nand_info->regbase + FIO_ECC_RPT_STA_OFFSET);
		}

		atomic_and(~0x2, &nand_info->irq_flag);
		wake_up(&nand_info->wq);
	}

	return IRQ_HANDLED;
}

/* this dma is used to transfer data between FIO FIFO and Memory. */
static irqreturn_t ambarella_fdma_isr_handler(int irq, void *dev_id)
{
	irqreturn_t				rval = IRQ_NONE;
	struct ambarella_nand_info		*nand_info;
	u32					int_src;

	nand_info = (struct ambarella_nand_info *)dev_id;

	int_src = readl_relaxed(nand_info->fdmaregbase + FDMA_INT_OFFSET);

	if (int_src & (1 << FIO_DMA_CHAN)) {
		nand_info->dma_status =
			readl_relaxed(nand_info->fdmaregbase + FDMA_STA_OFFSET);
		writel_relaxed(0, nand_info->fdmaregbase + FDMA_STA_OFFSET);
		writel_relaxed(0, nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET);

		atomic_and(~0x4, &nand_info->irq_flag);
		wake_up(&nand_info->wq);

		rval = IRQ_HANDLED;
	}

	return rval;
}

static void nand_amb_setup_dma_devmem(struct ambarella_nand_info *nand_info)
{
	u32					ctrl_val;
	u32					size = 0;

	/* init and enable fdma to transfer data betwee FIFO and Memory */
	if (nand_info->len > 16)
		ctrl_val = DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI | DMA_NODC_MN_BURST_SIZE;
	else
		ctrl_val = DMA_CHANX_CTR_WM | DMA_CHANX_CTR_NI | DMA_NODC_SP_BURST_SIZE;

	ctrl_val |= nand_info->len | DMA_CHANX_CTR_EN;
	ctrl_val &= ~DMA_CHANX_CTR_D;

	/* Setup main external DMA engine transfer */
	writel_relaxed(0, nand_info->fdmaregbase + FDMA_STA_OFFSET);

	writel_relaxed(nand_info->dmabase, nand_info->fdmaregbase + FDMA_SRC_OFFSET);
	writel_relaxed(nand_info->buf_phys, nand_info->fdmaregbase + FDMA_DST_OFFSET);

	if (nand_info->ecc_bits > 1) {
		/* Setup spare external DMA engine transfer */
		writel_relaxed(0x0, nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET);
		writel_relaxed(nand_info->dmabase, nand_info->fdmaregbase + FDMA_SPR_SRC_OFFSET);
		writel_relaxed(nand_info->spare_buf_phys, nand_info->fdmaregbase + FDMA_SPR_DST_OFFSET);
		writel_relaxed(nand_info->slen, nand_info->fdmaregbase + FDMA_SPR_CNT_OFFSET);
	}

	writel_relaxed(ctrl_val, nand_info->fdmaregbase + FDMA_CTR_OFFSET);

	/* init and enable fio-dma to transfer data between Nand and FIFO */
	writel_relaxed(nand_info->addr, nand_info->regbase + FIO_DMAADR_OFFSET);

	size = nand_info->len + nand_info->slen;
	if (size > 16) {
		ctrl_val = FIO_DMACTR_EN |
			FIO_DMACTR_FL |
			FIO_MN_BURST_SIZE |
			size;
	} else {
		ctrl_val = FIO_DMACTR_EN |
			FIO_DMACTR_FL |
			FIO_SP_BURST_SIZE |
			size;
	}
	writel(ctrl_val, nand_info->regbase + FIO_DMACTR_OFFSET);
}

static void nand_amb_setup_dma_memdev(struct ambarella_nand_info *nand_info)
{
	u32					ctrl_val, dma_burst_val, fio_burst_val;
	u32					size = 0;

	if (nand_info->ecc_bits > 1) {
		dma_burst_val = DMA_NODC_MN_BURST_SIZE8;
		fio_burst_val = FIO_MN_BURST_SIZE8;
	} else {
		dma_burst_val = DMA_NODC_MN_BURST_SIZE;
		fio_burst_val = FIO_MN_BURST_SIZE;
	}

	/* init and enable fdma to transfer data betwee FIFO and Memory */
	if (nand_info->len > 16)
		ctrl_val = DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI | dma_burst_val;
	else
		ctrl_val = DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI | DMA_NODC_SP_BURST_SIZE;

	ctrl_val |= nand_info->len | DMA_CHANX_CTR_EN;
	ctrl_val &= ~DMA_CHANX_CTR_D;

	/* Setup main external DMA engine transfer */
	writel_relaxed(0, nand_info->fdmaregbase + FDMA_STA_OFFSET);

	writel_relaxed(nand_info->buf_phys, nand_info->fdmaregbase + FDMA_SRC_OFFSET);
	writel_relaxed(nand_info->dmabase, nand_info->fdmaregbase + FDMA_DST_OFFSET);

	if (nand_info->ecc_bits > 1) {
		/* Setup spare external DMA engine transfer */
		writel_relaxed(0x0, nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET);
		writel_relaxed(nand_info->spare_buf_phys,
				nand_info->fdmaregbase + FDMA_SPR_SRC_OFFSET);
		writel_relaxed(nand_info->dmabase,
				nand_info->fdmaregbase + FDMA_SPR_DST_OFFSET);
		writel_relaxed(nand_info->slen,
				nand_info->fdmaregbase + FDMA_SPR_CNT_OFFSET);
	}

	writel_relaxed(ctrl_val, nand_info->fdmaregbase + FDMA_CTR_OFFSET);

	/* init and enable fio-dma to transfer data between Nand and FIFO */
	writel_relaxed(nand_info->addr, nand_info->regbase + FIO_DMAADR_OFFSET);

	size = nand_info->len + nand_info->slen;
	if (size > 16) {
		ctrl_val = FIO_DMACTR_EN | FIO_DMACTR_FL | fio_burst_val |
			FIO_DMACTR_RM | size;
	} else {
		ctrl_val = FIO_DMACTR_EN | FIO_DMACTR_FL | FIO_SP_BURST_SIZE |
			FIO_DMACTR_RM | size;
	}
	writel(ctrl_val, nand_info->regbase + FIO_DMACTR_OFFSET);
}

static int nand_amb_request(struct ambarella_nand_info *nand_info)
{
	struct mtd_info	*mtd = nand_to_mtd(&nand_info->chip);
	int					errorCode = 0;
	u32					cmd;
	u32					nand_ctr_reg = 0;
	u32					nand_cmd_reg = 0;
	u32					fio_ctr_reg = 0;
	long					timeout;

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
        aipc_mutex_lock(AMBA_IPC_MUTEX_NAND);

        enable_irq(nand_info->dma_irq);
        enable_irq(nand_info->cmd_irq);
        enable_irq(nand_info->fdma_irq);
#endif

	cmd = nand_info->cmd;

	nand_ctr_reg = nand_info->control_reg | NAND_CTR_WAS;

	if ((nand_info->nand_wp) &&
		(cmd == NAND_AMB_CMD_ERASE || cmd == NAND_AMB_CMD_COPYBACK ||
		 cmd == NAND_AMB_CMD_PROGRAM || cmd == NAND_AMB_CMD_READSTATUS))
			nand_ctr_reg &= ~NAND_CTR_WP;

	switch (cmd) {
	case NAND_AMB_CMD_RESET:
		nand_cmd_reg = NAND_AMB_CMD_RESET;
		writel_relaxed(nand_cmd_reg, nand_info->regbase + FLASH_CMD_OFFSET);
		break;

	case NAND_AMB_CMD_READID:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_READID;

		if (nand_info->id_cycles_5) {
			u32 nand_ext_ctr_reg = 0;

			nand_ext_ctr_reg = readl_relaxed(nand_info->regbase + FLASH_EX_CTR_OFFSET);
			nand_ext_ctr_reg |= NAND_EXT_CTR_I5;
			nand_ctr_reg &= ~(NAND_CTR_I4);
			writel_relaxed(nand_ext_ctr_reg, nand_info->regbase + FLASH_EX_CTR_OFFSET);
		}

		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		writel_relaxed(nand_cmd_reg, nand_info->regbase + FLASH_CMD_OFFSET);
		break;

	case NAND_AMB_CMD_READSTATUS:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_READSTATUS;
		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		writel_relaxed(nand_cmd_reg, nand_info->regbase + FLASH_CMD_OFFSET);
		break;

	case NAND_AMB_CMD_ERASE:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_ERASE;
		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		writel_relaxed(nand_cmd_reg, nand_info->regbase + FLASH_CMD_OFFSET);
		break;

	case NAND_AMB_CMD_COPYBACK:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);
		nand_ctr_reg |= NAND_CTR_CE;
		nand_cmd_reg = nand_info->addr | NAND_AMB_CMD_COPYBACK;
		writel_relaxed(nand_info->dst, nand_info->regbase + FLASH_CFI_OFFSET);
		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		writel_relaxed(nand_cmd_reg, nand_info->regbase + FLASH_CMD_OFFSET);
		break;

	case NAND_AMB_CMD_READ:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);

		if (nand_amb_is_hw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_bch(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;

			/* Clean Flash_IO_ecc_rpt_status Register */
			writel_relaxed(0x0, nand_info->regbase + FIO_ECC_RPT_STA_OFFSET);
		} else if (nand_amb_is_sw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_dsm(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;
		} else {
			if (nand_info->area == MAIN_ECC)
				nand_ctr_reg |= (NAND_CTR_SE);
			else if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC)
				nand_ctr_reg |= (NAND_CTR_SE | NAND_CTR_SA);

			fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);
			fio_ctr_reg &= ~(FIO_CTR_CO | FIO_CTR_RS);

			if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC  ||
				nand_info->area == MAIN_ECC)
				fio_ctr_reg |= (FIO_CTR_RS);

			switch (nand_info->ecc) {
			case EC_MDSE:
				nand_ctr_reg |= NAND_CTR_EC_SPARE;
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MESD:
				nand_ctr_reg |= NAND_CTR_EC_MAIN;
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MESE:
				nand_ctr_reg |=	(NAND_CTR_EC_MAIN | NAND_CTR_EC_SPARE);
				fio_ctr_reg |= FIO_CTR_CO;
				break;
			case EC_MDSD:
			default:
				break;
			}

			writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);
		}

		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		nand_amb_setup_dma_devmem(nand_info);

		break;

	case NAND_AMB_CMD_PROGRAM:
		nand_ctr_reg |= NAND_CTR_A(nand_info->addr_hi);

		if (nand_amb_is_hw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_bch(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;

			/* Clean Flash_IO_ecc_rpt_status Register */
			writel_relaxed(0x0, nand_info->regbase + FIO_ECC_RPT_STA_OFFSET);
		} else if (nand_amb_is_sw_bch(nand_info)) {
			/* Setup FIO DMA Control Register */
			nand_amb_enable_dsm(nand_info);
			/* in dual space mode,enable the SE bit */
			nand_ctr_reg |= NAND_CTR_SE;
		} else {
			if (nand_info->area == MAIN_ECC)
				nand_ctr_reg |= (NAND_CTR_SE);
			else if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC)
				nand_ctr_reg |= (NAND_CTR_SE | NAND_CTR_SA);

			fio_ctr_reg = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);
			fio_ctr_reg &= ~(FIO_CTR_CO | FIO_CTR_RS);

			if (nand_info->area == SPARE_ONLY ||
				nand_info->area == SPARE_ECC  ||
				nand_info->area == MAIN_ECC)
				fio_ctr_reg |= (FIO_CTR_RS);

			switch (nand_info->ecc) {
			case EG_MDSE :
				nand_ctr_reg |= NAND_CTR_EG_SPARE;
				break;
			case EG_MESD :
				nand_ctr_reg |= NAND_CTR_EG_MAIN;
				break;
			case EG_MESE :
				nand_ctr_reg |= (NAND_CTR_EG_MAIN | NAND_CTR_EG_SPARE);
				break;
			case EG_MDSD:
			default:
				break;
			}

			writel_relaxed(fio_ctr_reg, nand_info->regbase + FIO_CTR_OFFSET);
		}
		writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
		nand_amb_setup_dma_memdev(nand_info);

		break;

	default:
		dev_warn(nand_info->dev,
			"%s: wrong command %d!\n", __func__, cmd);
		errorCode = -EINVAL;
		goto nand_amb_request_done;
		break;
	}

	if (cmd == NAND_AMB_CMD_READ || cmd == NAND_AMB_CMD_PROGRAM) {
		timeout = wait_event_timeout(nand_info->wq,
			atomic_read(&nand_info->irq_flag) == 0x0, 1 * HZ);
		if (timeout <= 0) {
			errorCode = -EBUSY;
			dev_err(nand_info->dev, "%s: cmd=0x%x timeout 0x%08x\n",
				__func__, cmd, atomic_read(&nand_info->irq_flag));
		} else {
			dev_dbg(nand_info->dev, "%ld jiffies left.\n", timeout);
		}

		if (nand_info->dma_status & (DMA_CHANX_STA_OE | DMA_CHANX_STA_ME |
			DMA_CHANX_STA_BE | DMA_CHANX_STA_RWE |
			DMA_CHANX_STA_AE)) {
			dev_err(nand_info->dev,
				"%s: Errors happend in DMA transaction %d!\n",
				__func__, nand_info->dma_status);
			errorCode = -EIO;
			goto nand_amb_request_done;
		}

		if (nand_amb_is_hw_bch(nand_info)) {
			if (cmd == NAND_AMB_CMD_READ) {
				if (nand_info->fio_ecc_sta & FIO_ECC_RPT_FAIL) {
					int ret;

					/* Workaround for some chips which will
					 * report ECC failed for blank page. */
					if (FIO_SUPPORT_SKIP_BLANK_ECC)
						ret = -1;
					else
						ret = nand_bch_check_blank_page(nand_info);

					if (ret < 0) {
						mtd->ecc_stats.failed++;
						dev_err(nand_info->dev,
							"BCH corrected failed (0x%08x), addr is 0x[%x]!\n",
							nand_info->fio_ecc_sta, nand_info->addr);
					}
				} else if (nand_info->fio_ecc_sta & FIO_ECC_RPT_ERR) {
					unsigned int corrected = 1;
					if (NAND_ECC_RPT_NUM_SUPPORT) {
						corrected = (nand_info->fio_ecc_sta >> 16) & 0x000F;
						dev_info(nand_info->dev, "BCH correct [%d]bit in block[%d]\n",
						corrected, (nand_info->fio_ecc_sta & 0x00007FFF));
					} else {
						/* once bitflip and data corrected happened, BCH will keep on
						 * to report bitflip in following read operations, even though
						 * there is no bitflip happened really. So this is a workaround
						 * to get it back. */
						nand_amb_corrected_recovery(nand_info);
					}
					mtd->ecc_stats.corrected += corrected;
				}
			} else if (cmd == NAND_AMB_CMD_PROGRAM) {
				if (nand_info->fio_ecc_sta & FIO_ECC_RPT_FAIL) {
					dev_err(nand_info->dev,
						"BCH program program failed (0x%08x)!\n",
						nand_info->fio_ecc_sta);
				}
			}
		}

		if ((nand_info->fio_dma_sta & FIO_DMASTA_RE)
			|| (nand_info->fio_dma_sta & FIO_DMASTA_AE)
			|| !(nand_info->fio_dma_sta & FIO_DMASTA_DN)) {
			u32 block_addr;
			block_addr = nand_info->addr /
					mtd->erasesize *
					mtd->erasesize;
			dev_err(nand_info->dev,
				"%s: dma_status=0x%08x, cmd=0x%x, addr_hi=0x%x, "
				"addr=0x%x, dst=0x%x, buf=0x%x, "
				"len=0x%x, area=0x%x, ecc=0x%x, "
				"block addr=0x%x!\n",
				__func__,
				nand_info->fio_dma_sta,
				cmd,
				nand_info->addr_hi,
				nand_info->addr,
				nand_info->dst,
				(u32)nand_info->buf_phys,
				nand_info->len,
				nand_info->area,
				nand_info->ecc,
				block_addr);
			errorCode = -EIO;
			goto nand_amb_request_done;
		}
	} else {
		/* just wait cmd irq, no care about both DMA irqs */
		timeout = wait_event_timeout(nand_info->wq,
			(atomic_read(&nand_info->irq_flag) & 0x1) == 0x0, 1 * HZ);
		if (timeout <= 0) {
			errorCode = -EBUSY;
			dev_err(nand_info->dev, "%s: cmd=0x%x timeout 0x%08x\n",
				__func__, cmd, atomic_read(&nand_info->irq_flag));
			goto nand_amb_request_done;
		} else {
			dev_dbg(nand_info->dev, "%ld jiffies left.\n", timeout);

			if (cmd == NAND_AMB_CMD_READID) {
				u32 id = readl_relaxed(nand_info->regbase +
					FLASH_ID_OFFSET);
				if (nand_info->id_cycles_5) {
					u32 id_5 = readl_relaxed(nand_info->regbase +
					FLASH_EX_ID_OFFSET);
					nand_info->dmabuf[4] = (unsigned char) (id_5 & 0xFF);
				}
				nand_info->dmabuf[0] = (unsigned char) (id >> 24);
				nand_info->dmabuf[1] = (unsigned char) (id >> 16);
				nand_info->dmabuf[2] = (unsigned char) (id >> 8);
				nand_info->dmabuf[3] = (unsigned char) id;
			} else if (cmd == NAND_AMB_CMD_READSTATUS) {
				*nand_info->dmabuf = readl_relaxed(nand_info->regbase +
					FLASH_STA_OFFSET);
			}
		}
	}

nand_amb_request_done:
	atomic_set(&nand_info->irq_flag, 0x7);
	nand_info->dma_status = 0;
	/* Avoid to flush previous error info */
	if (nand_info->err_code == 0)
		nand_info->err_code = errorCode;

	if ((nand_info->nand_wp) &&
		(cmd == NAND_AMB_CMD_ERASE || cmd == NAND_AMB_CMD_COPYBACK ||
		 cmd == NAND_AMB_CMD_PROGRAM || cmd == NAND_AMB_CMD_READSTATUS)) {
			nand_ctr_reg |= NAND_CTR_WP;
			writel_relaxed(nand_ctr_reg, nand_info->regbase + FLASH_CTR_OFFSET);
	}

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
        disable_irq(nand_info->dma_irq);
        disable_irq(nand_info->cmd_irq);
        disable_irq(nand_info->fdma_irq);

        aipc_mutex_unlock(AMBA_IPC_MUTEX_NAND);
#else
	if ((cmd == NAND_AMB_CMD_READ || cmd == NAND_AMB_CMD_PROGRAM)
		&& nand_amb_is_hw_bch(nand_info))
		nand_amb_disable_bch(nand_info);
#endif

	return errorCode;
}

int nand_amb_reset(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_RESET;

	return nand_amb_request(nand_info);
}

int nand_amb_read_id(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_READID;
	nand_info->addr_hi = 0;
	nand_info->addr = 0;

	return nand_amb_request(nand_info);
}

int nand_amb_read_status(struct ambarella_nand_info *nand_info)
{
	nand_info->cmd = NAND_AMB_CMD_READSTATUS;
	nand_info->addr_hi = 0;
	nand_info->addr = 0;

	return nand_amb_request(nand_info);
}

int nand_amb_erase(struct ambarella_nand_info *nand_info, u32 page_addr)
{
	struct mtd_info	*mtd = nand_to_mtd(&nand_info->chip);
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u64					addr64;

	addr64 = (u64)(page_addr * mtd->writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	nand_info->cmd = NAND_AMB_CMD_ERASE;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;

	/* Fix dual space mode bug */
	if (nand_info->ecc_bits > 1)
		writel_relaxed(nand_info->addr, nand_info->regbase + FIO_DMAADR_OFFSET);

	errorCode = nand_amb_request(nand_info);

	return errorCode;
}

int nand_amb_read_data(struct ambarella_nand_info *nand_info,
	u32 page_addr, dma_addr_t buf_dma, u8 area)
{
	struct mtd_info	*mtd = nand_to_mtd(&nand_info->chip);
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u32					len;
	u64					addr64;
	u8					ecc = 0;

	addr64 = (u64)(page_addr * mtd->writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	switch (area) {
	case MAIN_ONLY:
		ecc = EC_MDSD;
		len = mtd->writesize;
		break;
	case MAIN_ECC:
		ecc = EC_MESD;
		len = mtd->writesize;
		break;
	case SPARE_ONLY:
		ecc = EC_MDSD;
		len = mtd->oobsize;
		break;
	case SPARE_ECC:
		ecc = EC_MDSE;
		len = mtd->oobsize;
		break;
	default:
		dev_err(nand_info->dev, "%s: Wrong area.\n", __func__);
		errorCode = -EINVAL;
		goto nand_amb_read_page_exit;
		break;
	}

	nand_info->slen = 0;
	if (nand_info->ecc_bits > 1) {
		/* when use BCH, the EG and EC should be 0 */
		ecc = 0;
		len = mtd->writesize;
		nand_info->slen = mtd->oobsize;
		nand_info->spare_buf_phys = buf_dma + len;
	}

	nand_info->cmd = NAND_AMB_CMD_READ;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;
	nand_info->buf_phys = buf_dma;
	nand_info->len = len;
	nand_info->area = area;
	nand_info->ecc = ecc;

	errorCode = nand_amb_request(nand_info);

nand_amb_read_page_exit:
	return errorCode;
}

int nand_amb_write_data(struct ambarella_nand_info *nand_info,
	u32 page_addr, dma_addr_t buf_dma, u8 area)
{
	struct mtd_info	*mtd = nand_to_mtd(&nand_info->chip);
	int					errorCode = 0;
	u32					addr_hi;
	u32					addr;
	u32					len;
	u64					addr64;
	u8					ecc;

	addr64 = (u64)(page_addr * mtd->writesize);
	addr_hi = (u32)(addr64 >> 32);
	addr = (u32)addr64;

	switch (area) {
	case MAIN_ONLY:
		ecc = EG_MDSD;
		len = mtd->writesize;
		break;
	case MAIN_ECC:
		ecc = EG_MESD;
		len = mtd->writesize;
		break;
	case SPARE_ONLY:
		ecc = EG_MDSD;
		len = mtd->oobsize;
		break;
	case SPARE_ECC:
		ecc = EG_MDSE;
		len = mtd->oobsize;
		break;
	default:
		dev_err(nand_info->dev, "%s: Wrong area.\n", __func__);
		errorCode = -EINVAL;
		goto nand_amb_write_page_exit;
		break;
	}

	nand_info->slen = 0;
	if (nand_info->ecc_bits > 1) {
		/* when use BCH, the EG and EC should be 0 */
		ecc = 0;
		len = mtd->writesize;
		nand_info->slen = mtd->oobsize;
		nand_info->spare_buf_phys = buf_dma + len;
	}
	nand_info->cmd = NAND_AMB_CMD_PROGRAM;
	nand_info->addr_hi = addr_hi;
	nand_info->addr = addr;
	nand_info->buf_phys = buf_dma;
	nand_info->len = len;
	nand_info->area = area;
	nand_info->ecc = ecc;

	errorCode = nand_amb_request(nand_info);

nand_amb_write_page_exit:
	return errorCode;
}


/* ==========================================================================*/
static uint8_t amb_nand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip	*chip = mtd_to_nand(mtd);
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);
	uint8_t					*data;

	data = nand_info->dmabuf + nand_info->dma_bufpos;
	nand_info->dma_bufpos++;

	return *data;
}

static u16 amb_nand_read_word(struct mtd_info *mtd)
{
	struct nand_chip	*chip = mtd_to_nand(mtd);
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);
	u16					*data;

	data = (u16 *)(nand_info->dmabuf + nand_info->dma_bufpos);
	nand_info->dma_bufpos += 2;

	return *data;
}

static void amb_nand_read_buf(struct mtd_info *mtd, uint8_t *buf, int len)
{
	struct nand_chip	*chip = mtd_to_nand(mtd);
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);

	BUG_ON((nand_info->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE);

	memcpy(buf, nand_info->dmabuf + nand_info->dma_bufpos, len);
	nand_info->dma_bufpos += len;
}

static void amb_nand_write_buf(struct mtd_info *mtd,
	const uint8_t *buf, int len)
{
	struct nand_chip	*chip = mtd_to_nand(mtd);
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);

	BUG_ON((nand_info->dma_bufpos + len) > AMBARELLA_NAND_DMA_BUFFER_SIZE);

	memcpy(nand_info->dmabuf + nand_info->dma_bufpos, buf, len);
	nand_info->dma_bufpos += len;
}

static void amb_nand_select_chip(struct mtd_info *mtd, int chip)
{
	struct nand_chip *nand = mtd_to_nand(mtd);
	struct ambarella_nand_info *nand_info = nand_get_controller_data(nand);

	if (chip > 0) {
		dev_err(nand_info->dev,
			"%s: Multi-Chip isn't supported yet.\n", __func__);
	}
}

static void amb_nand_cmd_ctrl(struct mtd_info *mtd, int dat, unsigned int ctrl)
{

}

static int amb_nand_dev_ready(struct mtd_info *mtd)
{
	struct nand_chip	*chip = mtd_to_nand(mtd);

	chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);

	return (chip->read_byte(mtd) & NAND_STATUS_READY) ? 1 : 0;
}

static int amb_nand_waitfunc(struct mtd_info *mtd, struct nand_chip *chip)
{
	int					status = 0;
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);

	/* ambarella nand controller has waited for the command completion,
	  * but still need to check the nand chip's status
	  */
	if (nand_info->err_code)
		status = NAND_STATUS_FAIL;
	else {
		chip->cmdfunc(mtd, NAND_CMD_STATUS, -1, -1);
		status = chip->read_byte(mtd);
	}

	return status;
}

static void amb_nand_cmdfunc(struct mtd_info *mtd, unsigned command,
	int column, int page_addr)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_info *nand_info = nand_get_controller_data(chip);

	nand_info->err_code = 0;

	switch(command) {
	case NAND_CMD_RESET:
		nand_amb_reset(nand_info);
		break;
	case NAND_CMD_READID:
		nand_info->dma_bufpos = 0;
		nand_amb_read_id(nand_info);
		break;
	case NAND_CMD_STATUS:
		nand_info->dma_bufpos = 0;
		nand_amb_read_status(nand_info);
		break;
	case NAND_CMD_ERASE1:
		nand_amb_erase(nand_info, page_addr);
		break;
	case NAND_CMD_ERASE2:
		break;
	case NAND_CMD_READOOB:
		nand_info->dma_bufpos = column;
		if (nand_info->ecc_bits > 1) {
			u8 area = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;
			nand_info->dma_bufpos = mtd->writesize;
			nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr, area);
		} else {
			nand_amb_read_data(nand_info, page_addr,
					nand_info->dmaaddr, SPARE_ONLY);
		}
		break;
	case NAND_CMD_READ0:
	{
		u8 area = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;

		nand_info->dma_bufpos = column;
		nand_amb_read_data(nand_info, page_addr, nand_info->dmaaddr, area);
		if (nand_info->ecc_bits == 1)
			nand_amb_read_data(nand_info, page_addr,
				nand_info->dmaaddr + mtd->writesize, SPARE_ONLY);

		break;
	}
	case NAND_CMD_SEQIN:
		nand_info->dma_bufpos = column;
		nand_info->seqin_column = column;
		nand_info->seqin_page_addr = page_addr;
		break;
	case NAND_CMD_PAGEPROG:
	{
		u32 mn_area, sp_area, offset;

		mn_area = nand_info->soft_ecc ? MAIN_ONLY : MAIN_ECC;
		sp_area = nand_amb_is_hw_bch(nand_info) ? SPARE_ECC : SPARE_ONLY;
		offset = (nand_info->ecc_bits > 1) ? 0 : mtd->writesize;

		if (nand_info->seqin_column < mtd->writesize) {
			nand_amb_write_data(nand_info,
				nand_info->seqin_page_addr,
				nand_info->dmaaddr, mn_area);
			if (nand_info->soft_ecc && nand_info->ecc_bits == 1) {
				nand_amb_write_data(nand_info,
					nand_info->seqin_page_addr,
					nand_info->dmaaddr + mtd->writesize,
					sp_area);
			}
		} else {
			nand_amb_write_data(nand_info,
				nand_info->seqin_page_addr,
				nand_info->dmaaddr + offset,
				sp_area);
		}
		break;
	}
	default:
		dev_err(nand_info->dev, "%s: 0x%x, %d, %d\n",
				__func__, command, column, page_addr);
		BUG();
		break;
	}
}

static void amb_nand_hwctl(struct mtd_info *mtd, int mode)
{
}

static int amb_nand_calculate_ecc(struct mtd_info *mtd,
		const u_char *buf, u_char *code)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_info *nand_info = nand_get_controller_data(chip);

	if (!nand_info->soft_ecc) {
		memset(code, 0xff, nand_info->chip.ecc.bytes);
	} else if (nand_info->ecc_bits == 1) {
		nand_calculate_ecc(mtd, buf, code);
		/* FIXME: the first two bytes ecc codes are swaped comparing
		 * to the ecc codes generated by our hardware, so we swap them
		 * here manually. But I don't know why they were swapped. */
		swap(code[0], code[1]);
	} else {
		u32 i, amb_eccsize;

		/* make it be compatible with hw bch */
		for (i = 0; i < nand_info->chip.ecc.size; i++)
			nand_info->bch_data[i] = bitrev8(buf[i]);

		memset(code, 0, nand_info->chip.ecc.bytes);

		amb_eccsize = nand_info->chip.ecc.size + nand_info->soft_bch_extra_size;
		encode_bch(nand_info->bch, nand_info->bch_data, amb_eccsize, code);

		/* make it be compatible with hw bch */
		for (i = 0; i < nand_info->chip.ecc.bytes; i++)
			code[i] = bitrev8(code[i]);
	}

	return 0;
}

static int amb_nand_correct_data(struct mtd_info *mtd, u_char *buf,
		u_char *read_ecc, u_char *calc_ecc)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct ambarella_nand_info *nand_info = nand_get_controller_data(chip);
	int errorCode = 0;

	/* if we use hardware ecc, any errors include DMA error and
	 * FIO DMA error, we consider it as a ecc error which will tell
	 * the caller the read fail. We have distinguish all the errors,
	 * but the nand_read_ecc only check the return value by this
	 * function. */
	if (!nand_info->soft_ecc)
		errorCode = nand_info->err_code;
	else if (nand_info->ecc_bits == 1)
		errorCode = nand_correct_data(mtd, buf, read_ecc, calc_ecc);
	else {
		struct nand_chip *chip = &nand_info->chip;
		u32 *errloc = nand_info->errloc;
		int amb_eccsize, i, count;

		for (i = 0; i < chip->ecc.bytes; i++) {
			nand_info->read_ecc_rev[i] = bitrev8(read_ecc[i]);
			nand_info->calc_ecc_rev[i] = bitrev8(calc_ecc[i]);
		}

		amb_eccsize = chip->ecc.size + nand_info->soft_bch_extra_size;
		count = decode_bch(nand_info->bch, NULL,
					amb_eccsize,
					nand_info->read_ecc_rev,
					nand_info->calc_ecc_rev,
					NULL, errloc);
		if (count > 0) {
			for (i = 0; i < count; i++) {
				if (errloc[i] < (amb_eccsize * 8)) {
					/* error is located in data, correct it */
					buf[errloc[i] >> 3] ^= (128 >> (errloc[i] & 7));
				}
				/* else error in ecc, no action needed */

				dev_dbg(nand_info->dev,
					"corrected bitflip %u\n", errloc[i]);
			}
		} else if (count < 0) {
			count = nand_bch_check_blank_page(nand_info);
			if (count < 0)
				dev_err(nand_info->dev, "ecc unrecoverable error\n");
		}

		errorCode = count;
	}

	return errorCode;
}

static int amb_nand_write_oob_std(struct mtd_info *mtd,
	struct nand_chip *chip, int page)
{
	struct ambarella_nand_info	*nand_info = nand_get_controller_data(chip);
	int					i, status;
	int	eccsteps;
	uint8_t *ecc_calc = chip->buffers->ecccalc;

	/* Our nand controller will write the generated ECC code into spare
	  * area automatically, so we should mark the ECC code which located
	  * in the eccpos.
	  * No point to do this with hw ecc, the controller will set code
	  * were it should be
	  */
	if (nand_info->soft_ecc) {
		eccsteps = chip->ecc.steps;
		for (i = 0; eccsteps; eccsteps--, i += chip->ecc.bytes) {
			if (chip->ecc.calculate)
				chip->ecc.calculate(mtd, NULL, &ecc_calc[i]);
			else
				amb_nand_calculate_ecc(mtd, NULL, &ecc_calc[i]);
			status = mtd_ooblayout_set_eccbytes(mtd, ecc_calc, chip->oob_poi, 0,
					 chip->ecc.total);
			if (status)
				return status;
		}
	}

	chip->cmdfunc(mtd, NAND_CMD_SEQIN, mtd->writesize, page);
	chip->write_buf(mtd, chip->oob_poi, mtd->oobsize);
	chip->cmdfunc(mtd, NAND_CMD_PAGEPROG, -1, -1);
	status = chip->waitfunc(mtd, chip);

	return status & NAND_STATUS_FAIL ? -EIO : 0;
}

/*
 * The encoding sequence in a byte is "LSB first".
 *
 * For each 2K page, there will be 2048 byte main data (B0 ~ B2047) and 64 byte
 * spare data (B2048 ~ B2111). Thus, each page is divided into 4 BCH blocks.
 * For example, B0~B511 and B2048~B2063 are grouped as the first BCH block.
 * B0 will be encoded first and B2053 will be encoded last.
 *
 * B2054 ~B2063 are used to store 10B parity data (precisely to say, 78 bits)
 * The 2 dummy bits are filled as 0 and located at the msb of B2063.
*/
static int ambarella_nand_init_soft_bch(struct ambarella_nand_info *nand_info)
{
	struct nand_chip *chip = &nand_info->chip;
	u32 amb_eccsize, eccbytes, m, t;

	amb_eccsize = chip->ecc.size + nand_info->soft_bch_extra_size;
	eccbytes = chip->ecc.bytes;

	m = fls(1 + 8 * amb_eccsize);
	t = (eccbytes * 8) / m;

	nand_info->bch = init_bch(m, t, 0);
	if (!nand_info->bch)
		return -EINVAL;

	nand_info->errloc = devm_kzalloc(nand_info->dev,
				t * sizeof(*nand_info->errloc), GFP_KERNEL);
	if (!nand_info->errloc)
		return -ENOMEM;

	nand_info->bch_data = devm_kzalloc(nand_info->dev,
					amb_eccsize, GFP_KERNEL);
	if (nand_info->bch_data == NULL)
		return -ENOMEM;

	/* asumming the 6 bytes data in spare area are all 0xff, in other
	 * words, we don't support to write anything except for ECC code
	 * into spare are. */
	memset(nand_info->bch_data + chip->ecc.size,
				0xff, nand_info->soft_bch_extra_size);

	return 0;
}

static void ambarella_nand_init_hw(struct ambarella_nand_info *nand_info)
{
	u32 val;

	/* reset FIO by RCT */
	ambarella_fio_rct_reset(nand_info);

	/* Reset FIO FIFO and then exit random read mode */
	val = readl_relaxed(nand_info->regbase + FIO_CTR_OFFSET);
	val |= FIO_CTR_RR;
	writel_relaxed(val, nand_info->regbase + FIO_CTR_OFFSET);
	/* wait for some time to make sure FIO FIFO reset is done */
	msleep(3);
	val &= ~FIO_CTR_RR;
	writel_relaxed(val, nand_info->regbase + FIO_CTR_OFFSET);

	/* init fdma to avoid dummy irq */
	writel_relaxed(0, nand_info->fdmaregbase + FDMA_STA_OFFSET);
	writel_relaxed(0, nand_info->fdmaregbase + FDMA_SPR_STA_OFFSET);
	writel_relaxed(DMA_CHANX_CTR_WM | DMA_CHANX_CTR_RM | DMA_CHANX_CTR_NI,
			nand_info->fdmaregbase + FDMA_CTR_OFFSET);

	writel_relaxed(nand_info->control_reg, nand_info->regbase + FIO_CTR_OFFSET);

	amb_nand_set_timing(nand_info);
}

static int ambarella_nand_config_flash(struct ambarella_nand_info *nand_info)
{
	int					errorCode = 0;

	/* control_reg will be uesd when real operation to NAND is performed */

	/* Calculate row address cycyle according to whether the page number
	  * of the nand is greater than 65536 */
	if ((nand_info->chip.chip_shift - nand_info->chip.page_shift) > 16)
		nand_info->control_reg |= NAND_CTR_P3;
	else
		nand_info->control_reg &= ~NAND_CTR_P3;

	nand_info->control_reg &= ~NAND_CTR_SZ_8G;
	switch (nand_info->chip.chipsize) {
	case 8 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_64M;
		break;
	case 16 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_128M;
		break;
	case 32 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_256M;
		break;
	case 64 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_512M;
		break;
	case 128 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_1G;
		break;
	case 256 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_2G;
		break;
	case 512 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_4G;
		break;
	case 1024 * 1024 * 1024:
		nand_info->control_reg |= NAND_CTR_SZ_8G;
		break;
	default:
		dev_err(nand_info->dev,
			"Unexpected NAND flash chipsize %lld. Aborting\n",
			nand_info->chip.chipsize);
		errorCode = -ENXIO;
		break;
	}

	return errorCode;
}

static int ambarella_nand_init_chip(struct ambarella_nand_info *nand_info,
		struct device_node *np)
{
	struct nand_chip *chip = &nand_info->chip;
	u32 poc;

	regmap_read(nand_info->reg_rct, SYS_CONFIG_OFFSET, &poc);

	/* if ecc is generated by software, the ecc bits num will
	 * be defined in FDT. */
	if (!nand_info->soft_ecc) {
		if (of_find_property(np, "amb,no-bch", NULL)) {
			nand_info->ecc_bits = 1;
		} else if (poc & SYS_CONFIG_NAND_ECC_BCH_EN) {
			if (poc & SYS_CONFIG_NAND_ECC_SPARE_2X)
				nand_info->ecc_bits = 8;
			else
				nand_info->ecc_bits = 6;
		} else {
			nand_info->ecc_bits = 1;
		}
	}

	dev_info(nand_info->dev, "in %secc-[%d]bit mode\n",
		nand_info->soft_ecc ? "soft " : "", nand_info->ecc_bits);

	nand_info->control_reg = 0;
	if (poc & SYS_CONFIG_NAND_READ_CONFIRM)
		nand_info->control_reg |= NAND_CTR_RC;
	if (poc & SYS_CONFIG_NAND_PAGE_SIZE)
		nand_info->control_reg |= (NAND_CTR_C2 | NAND_CTR_SZ_8G);
	/*
	  * Always use P3 and I4 to support all NAND,
	  * but we will adjust them after read ID from NAND. */
	nand_info->control_reg |= (NAND_CTR_P3 | NAND_CTR_I4 | NAND_CTR_IE);
	nand_info->id_cycles_5 = NAND_READ_ID5;

	if(nand_info->nand_wp)
		nand_info->control_reg |= NAND_CTR_WP;

	chip->chip_delay = 0;
	chip->controller = &nand_info->controller;
	chip->read_byte = amb_nand_read_byte;
	chip->read_word = amb_nand_read_word;
	chip->write_buf = amb_nand_write_buf;
	chip->read_buf = amb_nand_read_buf;
	chip->select_chip = amb_nand_select_chip;
	chip->cmd_ctrl = amb_nand_cmd_ctrl;
	chip->dev_ready = amb_nand_dev_ready;
	chip->waitfunc = amb_nand_waitfunc;
	chip->cmdfunc = amb_nand_cmdfunc;
	chip->options |= NAND_NO_SUBPAGE_WRITE;

	nand_set_flash_node(chip, np);
	return 0;
}

static int ambarella_nand_init_chipecc(struct ambarella_nand_info *nand_info)
{
	struct nand_chip *chip = &nand_info->chip;
	struct mtd_info	*mtd = nand_to_mtd(chip);
	int errorCode = 0;

	/* sanity check */
	BUG_ON(nand_info->ecc_bits != 1
		&& nand_info->ecc_bits != 6
		&& nand_info->ecc_bits != 8);
	BUG_ON(mtd->writesize != 2048 && mtd->writesize != 512);
	BUG_ON(nand_info->ecc_bits == 8 && mtd->oobsize < 128);

	/* Don't override ecc.mode if already detected during nand_scan_ident() */
	if (chip->ecc.mode == 0)
		chip->ecc.mode = NAND_ECC_HW;

	chip->ecc.strength = nand_info->ecc_bits;

	switch (nand_info->ecc_bits) {
	case 8:
		chip->ecc.size = 512;
		chip->ecc.bytes = 13;
		nand_info->soft_bch_extra_size = 19;
		mtd_set_ooblayout(mtd, &amb_ecc8_lp_ooblayout_ops);
		break;
	case 6:
		chip->ecc.size = 512;
		chip->ecc.bytes = 10;
		nand_info->soft_bch_extra_size = 6;
		mtd_set_ooblayout(mtd, &amb_ecc6_lp_ooblayout_ops);
		break;
	case 1:
		chip->ecc.size = 512;
		chip->ecc.bytes = 5;
		if (mtd->writesize == 2048)
			mtd_set_ooblayout(mtd, &amb_ecc1_lp_ooblayout_ops);
		else
			mtd_set_ooblayout(mtd, &amb_ecc1_sp_ooblayout_ops);
		break;
	}

	if (nand_amb_is_sw_bch(nand_info)) {
		errorCode = ambarella_nand_init_soft_bch(nand_info);
		if (errorCode < 0)
			return errorCode;
		/* bootloader may have enabled hw BCH, we must disable it here */
		nand_amb_enable_dsm(nand_info);
	}

	chip->ecc.hwctl = amb_nand_hwctl;
	chip->ecc.calculate = amb_nand_calculate_ecc;
	chip->ecc.correct = amb_nand_correct_data;
	chip->ecc.write_oob = amb_nand_write_oob_std;

	return 0;
}

static int ambarella_nand_get_resource(
	struct ambarella_nand_info *nand_info, struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	int errorCode = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource for fio_reg!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}

	nand_info->regbase =
		devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!nand_info->regbase) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		errorCode = -ENOMEM;
		goto nand_get_resource_err_exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource for fdma_reg!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}

	nand_info->fdmaregbase =
		devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!nand_info->fdmaregbase) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		errorCode = -ENOMEM;
		goto nand_get_resource_err_exit;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource for fifo base!\n");
		errorCode = -ENXIO;
		goto nand_get_resource_err_exit;
	}
	nand_info->dmabase = res->start;

	nand_info->cmd_irq = platform_get_irq(pdev, 0);
	if (nand_info->cmd_irq < 0) {
		dev_err(&pdev->dev, "no irq for cmd_irq!\n");
		errorCode = -ENODEV;
		goto nand_get_resource_err_exit;
	}

	nand_info->dma_irq = platform_get_irq(pdev, 1);
	if (nand_info->dma_irq < 0) {
		dev_err(&pdev->dev, "no irq for dma_irq!\n");
		errorCode = -ENODEV;
		goto nand_get_resource_err_exit;
	}

	nand_info->fdma_irq = platform_get_irq(pdev, 2);
	if (nand_info->fdma_irq < 0) {
		dev_err(&pdev->dev, "no irq for fdma_irq!\n");
		errorCode = -ENODEV;
		goto nand_get_resource_err_exit;
	}

	nand_info->reg_rct = syscon_regmap_lookup_by_phandle(np, "amb,rct-regmap");
	if (IS_ERR(nand_info->reg_rct)) {
		dev_err(&pdev->dev, "no rct regmap!\n");
		errorCode = PTR_ERR(nand_info->reg_rct);
		goto nand_get_resource_err_exit;
	}

	nand_info->nand_wp = !!of_find_property(np, "amb,enable-wp", NULL);

	errorCode = of_property_read_u32_array(np, "amb,timing",
			nand_info->timing, 6);
	if (errorCode < 0) {
		dev_dbg(&pdev->dev, "No timing defined!\n");
		memset(nand_info->timing, 0x0, sizeof(nand_info->timing));
	}

	nand_info->ecc_bits = 0;
	of_property_read_u32(np, "amb,soft-ecc", &nand_info->ecc_bits);
	if (nand_info->ecc_bits > 0)
		nand_info->soft_ecc = true;

	ambarella_nand_init_hw(nand_info);

	errorCode = request_irq(nand_info->cmd_irq, nand_fiocmd_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fio_cmd_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fio_cmd_irq %d!\n",
			nand_info->cmd_irq);
		goto nand_get_resource_err_exit;
	}

	errorCode = request_irq(nand_info->dma_irq, nand_fiodma_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fio_dma_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fio_dma_irq %d!\n",
			nand_info->dma_irq);
		goto nand_get_resource_free_fiocmd_irq;
	}

	errorCode = request_irq(nand_info->fdma_irq, ambarella_fdma_isr_handler,
			IRQF_SHARED | IRQF_TRIGGER_HIGH,
			"fdma_irq", nand_info);
	if (errorCode < 0) {
		dev_err(&pdev->dev, "Could not register fdma_irq %d!\n",
			nand_info->dma_irq);
		goto nand_get_resource_free_fiodma_irq;
	}

	return 0;

nand_get_resource_free_fiodma_irq:
	free_irq(nand_info->dma_irq, nand_info);

nand_get_resource_free_fiocmd_irq:
	free_irq(nand_info->cmd_irq, nand_info);

nand_get_resource_err_exit:
	return errorCode;
}

static void ambarella_nand_put_resource(struct ambarella_nand_info *nand_info)
{
	free_irq(nand_info->fdma_irq, nand_info);
	free_irq(nand_info->dma_irq, nand_info);
	free_irq(nand_info->cmd_irq, nand_info);
}

static int ambarella_nand_probe(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;
	struct mtd_info				*mtd;
	struct nand_chip *chip;

	nand_info = kzalloc(sizeof(struct ambarella_nand_info), GFP_KERNEL);
	if (nand_info == NULL) {
		dev_err(&pdev->dev, "kzalloc for nand nand_info failed!\n");
		errorCode = - ENOMEM;
		goto ambarella_nand_probe_exit;
	}

	nand_info->dev = &pdev->dev;
	spin_lock_init(&nand_info->controller.lock);
	init_waitqueue_head(&nand_info->controller.wq);
	init_waitqueue_head(&nand_info->wq);
	sema_init(&nand_info->system_event_sem, 1);
	atomic_set(&nand_info->irq_flag, 0x7);

	nand_info->dmabuf = dma_alloc_coherent(nand_info->dev,
		AMBARELLA_NAND_DMA_BUFFER_SIZE,
		&nand_info->dmaaddr, GFP_KERNEL);
	if (nand_info->dmabuf == NULL) {
		dev_err(&pdev->dev, "dma_alloc_coherent failed!\n");
		errorCode = -ENOMEM;
		goto ambarella_nand_probe_free_info;
	}
	BUG_ON(nand_info->dmaaddr & 0x7);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
        aipc_mutex_lock(AMBA_IPC_MUTEX_NAND);
#endif
	errorCode = ambarella_nand_get_resource(nand_info, pdev);
	if (errorCode < 0)
		goto ambarella_nand_probe_free_dma;

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
        disable_irq(nand_info->dma_irq);
        disable_irq(nand_info->cmd_irq);
        disable_irq(nand_info->fdma_irq);

        aipc_mutex_unlock(AMBA_IPC_MUTEX_NAND);
#endif

	ambarella_nand_init_chip(nand_info, pdev->dev.of_node);

	chip = &nand_info->chip;
	nand_set_controller_data(chip, nand_info);
	mtd = nand_to_mtd(chip);

	errorCode = nand_scan_ident(mtd, 1, NULL);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	if (chip->bbt_options & NAND_BBT_USE_FLASH)
		chip->bbt_options |= NAND_BBT_NO_OOB;

	errorCode = ambarella_nand_init_chipecc(nand_info);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	errorCode = ambarella_nand_config_flash(nand_info);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	errorCode = nand_scan_tail(mtd);
	if (errorCode)
		goto ambarella_nand_probe_mtd_error;

	mtd->name = "amba_nand";

	errorCode = mtd_device_parse_register(mtd, NULL, NULL, NULL, 0);
	if (errorCode < 0)
		goto ambarella_nand_probe_mtd_error;

	platform_set_drvdata(pdev, nand_info);

	nand_info->system_event.notifier_call = ambarella_nand_system_event;
	ambarella_register_event_notifier(&nand_info->system_event);

	return 0;

ambarella_nand_probe_mtd_error:
	ambarella_nand_put_resource(nand_info);

ambarella_nand_probe_free_dma:
	dma_free_coherent(nand_info->dev,
		AMBARELLA_NAND_DMA_BUFFER_SIZE,
		nand_info->dmabuf, nand_info->dmaaddr);

ambarella_nand_probe_free_info:
	kfree(nand_info);

ambarella_nand_probe_exit:

	return errorCode;
}

static int ambarella_nand_remove(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = (struct ambarella_nand_info *)platform_get_drvdata(pdev);

	if (nand_info) {
		ambarella_unregister_event_notifier(&nand_info->system_event);

		nand_release(nand_to_mtd(&nand_info->chip));

		ambarella_nand_put_resource(nand_info);

		dma_free_coherent(nand_info->dev,
			AMBARELLA_NAND_DMA_BUFFER_SIZE,
			nand_info->dmabuf, nand_info->dmaaddr);

		kfree(nand_info);
	}

	return errorCode;
}

#ifdef CONFIG_PM
static int ambarella_nand_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = platform_get_drvdata(pdev);
	disable_irq(nand_info->dma_irq);
	disable_irq(nand_info->cmd_irq);

	dev_dbg(&pdev->dev, "%s exit with %d @ %d\n",
		__func__, errorCode, state.event);

	return errorCode;
}

static int ambarella_nand_resume(struct platform_device *pdev)
{
	int					errorCode = 0;
	struct ambarella_nand_info		*nand_info;

	nand_info = platform_get_drvdata(pdev);
#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
	ambarella_nand_init_hw(nand_info);
#endif

	enable_irq(nand_info->dma_irq);
	enable_irq(nand_info->cmd_irq);

	dev_dbg(&pdev->dev, "%s exit with %d\n", __func__, errorCode);

	return errorCode;
}
#endif

static const struct of_device_id ambarella_nand_of_match[] = {
	{.compatible = "ambarella,nand", },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_nand_of_match);

static struct platform_driver amb_nand_driver = {
	.probe		= ambarella_nand_probe,
	.remove		= ambarella_nand_remove,
#ifdef CONFIG_PM
	.suspend	= ambarella_nand_suspend,
	.resume		= ambarella_nand_resume,
#endif
	.driver = {
		.name	= "ambarella-nand",
		.of_match_table = ambarella_nand_of_match,
	},
};
module_platform_driver(amb_nand_driver);

MODULE_AUTHOR("Cao Rongrong & Chien-Yang Chen");
MODULE_DESCRIPTION("Ambarella Media processor NAND Controller Driver");
MODULE_LICENSE("GPL");

