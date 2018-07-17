/*
 * arch/arm/plat-ambarella/include/plat/chip.h
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2013, Ambarella, Inc.
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

#ifndef __PLAT_AMBARELLA_CHIP_H__
#define __PLAT_AMBARELLA_CHIP_H__

/* ==========================================================================*/
#define A5S		(5100)
#define S2		(9000)
#define S2E		(9100)
#define S2L		(12000)
#define S3		(11000)
#define S3L		(13000)
#define S5		(15000)
#define H2		(15000)
#define S5L		(16000)

#define CHIP_ID(x)	((x / 1000))
#define CHIP_MAJOR(x)	((x / 100) % 10)
#define CHIP_MINOR(x)	((x / 10) % 10)

#if defined(CONFIG_ARCH_AMBARELLA_S2E)
#define CHIP_REV	S2E
#elif defined(CONFIG_ARCH_AMBARELLA_S2L)
#define CHIP_REV	S2L
#elif defined(CONFIG_ARCH_AMBARELLA_S3)
#define CHIP_REV	S3
#elif defined(CONFIG_ARCH_AMBARELLA_S3L)
#define CHIP_REV	S3L
#elif defined(CONFIG_ARCH_AMBARELLA_S5)
#define CHIP_REV	S5
#elif defined(CONFIG_ARCH_AMBARELLA_S5L)
#define CHIP_REV	S5L
#else
#error "Undefined CHIP_REV"
#endif

/* ==========================================================================*/
#if (CHIP_REV == A5S)
#define	CHIP_BROKEN_UNALIGNED_ACCESS	1
#endif

#if (CHIP_REV == S3)
#define	CHIP_FIX_2NDCORTEX_BOOT	1
#endif

/* ==========================================================================*/

#ifndef IOMEM
#define IOMEM(x)	((void __force __iomem *)(x))
#endif

#if (CHIP_REV == A5S)
#define DEFAULT_MEM_START		(0xc0000000)
#else
#define DEFAULT_MEM_START		(0x00000000)
#endif

/* Physical Address and Size */
#if (CHIP_REV == A5S) || (CHIP_REV == S2) || (CHIP_REV == S2E)
#define AHB_PHYS_BASE			(0x60000000)
#define APB_PHYS_BASE			(0x70000000)
#else
#define AHB_PHYS_BASE			(0xe0000000)
#define APB_PHYS_BASE			(0xe8000000)
#endif
#define AHB_SIZE			(0x01000000)
#define APB_SIZE			(0x01000000)

#define AXI_PHYS_BASE			(0xf0000000)
#define AXI_SIZE			(0x00030000)

#define DRAMC_PHYS_BASE			(0xdffe0000)
#define DRAMC_SIZE			(0x00020000)

#define DBGBUS_PHYS_BASE		(0xec000000)
#define DBGBUS_SIZE			(0x00200000)

#define DBGFMEM_PHYS_BASE		(0xee000000)
#define DBGFMEM_SIZE			(0x01000000)

/* ==========================================================================*/

#ifndef __ASSEMBLER__

extern struct proc_dir_entry *get_ambarella_proc_dir(void);

#endif

/* ==========================================================================*/

#endif /* __PLAT_AMBARELLA_CHIP_H__ */

