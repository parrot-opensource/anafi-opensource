/*
 * arch/arm/plat-ambarella/include/plat/rtc.h
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
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

#ifndef __PLAT_AMBARELLA_RTC_H__
#define __PLAT_AMBARELLA_RTC_H__

#include <plat/chip.h>

/* ==========================================================================*/
#if (CHIP_REV == A5S)
#define RTC_SUPPORT_30BITS_PASSED_SECONDS	1
#else
#define RTC_SUPPORT_30BITS_PASSED_SECONDS	0
#endif

#if (CHIP_REV == A5S)
#define RTC_POWER_LOST_DETECT			0
#else
#define RTC_POWER_LOST_DETECT			1
#endif

/* ==========================================================================*/
#if (CHIP_REV == A5S) || (CHIP_REV == S2) || (CHIP_REV == S2E)
#define RTC_OFFSET			0xD000
#else
#define RTC_OFFSET			0x15000
#endif
#define RTC_BASE			(APB_BASE + RTC_OFFSET)
#define RTC_REG(x)			(RTC_BASE + (x))

/* ==========================================================================*/
#define PWC_POS0_OFFSET			0x20
#define PWC_POS1_OFFSET			0x24
#define PWC_POS2_OFFSET			0x28

#define RTC_ALAT_WRITE_OFFSET		0x2C
#define RTC_CURT_WRITE_OFFSET		0x30
#define RTC_CURT_READ_OFFSET		0x34
#define RTC_ALAT_READ_OFFSET		0x38
#define RTC_STATUS_OFFSET			0x3C

#define PWC_RESET_OFFSET			0x40
#define PWC_WO_OFFSET				0x7C
#define PWC_DNALERT_OFFSET			0xA8
#define PWC_LBAT_OFFSET				0xAC
#define PWC_REG_WKUPC_OFFSET		0xB0
#define PWC_REG_STA_OFFSET			0xB4
#define PWC_SET_STATUS_OFFSET		0xC0
#define PWC_CLR_REG_WKUPC_OFFSET	0xC4
#define PWC_WKENC_OFFSET			0xC8
#define PWC_POS3_OFFSET				0xD0

#define PWC_ENP1C_OFFSET		0xD4
#define PWC_ENP2C_OFFSET		0xD8
#define PWC_ENP3C_OFFSET		0xDC
#define PWC_ENP1_OFFSET			0xE0
#define PWC_ENP2_OFFSET			0xE4
#define PWC_ENP3_OFFSET			0xE8
#define PWC_DISP1C_OFFSET		0xEC
#define PWC_DISP2C_OFFSET		0xF0
#define PWC_DISP3C_OFFSET		0xF4
#define PWC_DISP1_OFFSET		0xF8
#define PWC_DISP2_OFFSET		0xB8
#define PWC_DISP3_OFFSET		0xBC
#define PWC_BC_OFFSET			0xFC

/* ==========================================================================*/
/* RTC_STATUS_OFFSET */
#define RTC_STATUS_WKUP			0x8
#define RTC_STATUS_ALA_WK		0x4
#define RTC_STATUS_PC_RST		0x2
#define RTC_STATUS_RTC_CLK		0x1

/* RTC_PWC_REG_STA_OFFSET */
#define PWC_STA_LOSS_MASK		0x20
#define PWC_STA_ALARM_MASK		0x8
#define PWC_STA_SR_MASK			0x4

/* ==========================================================================*/

#endif /* __PLAT_AMBARELLA_RTC_H__ */

