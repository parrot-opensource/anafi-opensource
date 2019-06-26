/*
 * arch/arm/plat-ambarella/include/plat/dmic.h
 *
 * Author: XianqingZheng <xqzheng@ambarella.com>
 *
 * Copyright (C) 2004-2016, Ambarella, Inc.
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

#ifndef __PLAT_AMBARELLA_DMIC_H__
#define __PLAT_AMBARELLA_DMIC_H__

#define DMIC_ENABLE_OFFSET			0x00
#define AUDIO_CODEC_DP_RESET_OFFSET		0x04
#define DECIMATION_FACTOR_OFFSET		0x08
#define DMIC_STATAUS_OFFSET			0x0c
#define I2S_WPOS_OFFSET				0x10
#define CIC_MUTIPLIER_OFFSET			0x14
#define DMIC_CLK_DIV_OFFSET			0x100
#define DMIC_DATA_PHASE_OFFSET			0x104
#define DMIC_CLK_ENABLE_OFFSET			0x108
#define WIND_NS_FL_GM_CTRL_OFFSET		0x10C
#define WIND_NS_FL_CTRL_OFFSET			0x110
#define DMIC_I2S_CTRL_OFFSET			0x114
#define DEBUG_STATUS0_OFFSET			0x118
#define DEBUG_STATUS1_OFFSET			0x11C
#define DEBUG_STATUS2_OFFSET			0x120
#define DROOP_CP_FL_COFT0_OFFSET		0x200
#define DROOP_CP_FL_COFT63_OFFSET		0x2FC
#define HALF_BD_FL_COFT0_OFFSET			0x300
#define HALF_BD_FL_COFT63_OFFSET		0x3FC
#define WIND_NS_FL_HPF_COFT0_OFFSET		0x400
#define WIND_NS_FL_HPF_COFT67_OFFSET		0x50C
#define WIND_NS_FL_LPF_COFT0_OFFSET		0x510
#define WIND_NS_FL_LPF_COFT67_OFFSET		0x61C
#define WIND_NS_FL_WND_COFT0_OFFSET		0x620
#define WIND_NS_FL_WND_COFT16_OFFSET		0x660
#define WIND_NS_FL_VOC_HPF_COFT0_OFFSET		0x664
#define WIND_NS_FL_VOC_HPF_COFT16_OFFSET	0x6A4
#define WIND_NS_FL_VOC_LPF_COFT0_OFFSET		0x6A8
#define WIND_NS_FL_VOC_LPF_COFT10_OFFSET	0x6D0

#endif /* __PLAT_AMBARELLA_DMIC_H__ */

