/*
 * arch/arm/plat-ambarella/include/mach/io.h
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
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

#ifndef __ASM_ARCH_IO_H
#define __ASM_ARCH_IO_H

/* ==========================================================================*/
#include <mach/hardware.h>

/* ==========================================================================*/
#define IO_SPACE_LIMIT		0xffffffff

#define __io(a)			((void __iomem *)(a))
#define __mem_pci(a)		(a)

/* ==========================================================================*/
#ifndef __ASSEMBLER__

#define amba_readb(v)		__raw_readb(v)
#define amba_readw(v)		__raw_readw(v)
#define amba_writeb(v,d)	__raw_writeb(d,v)
#define amba_writew(v,d)	__raw_writew(d,v)
#define amba_readl(v)		__raw_readl((volatile void __iomem *)v)
#define amba_writel(v,d)	__raw_writel(d, (volatile void __iomem *)v)

static inline void __amba_read2w(volatile void __iomem *address,
	u16 *value1, u16 *value2)
{
	volatile void __iomem *base;
	u32 tmpreg;

	BUG_ON((u32)address & 0x03);

	base = (volatile void __iomem *)((u32)address & 0xFFFFFFFC);
	tmpreg = amba_readl(base);
	*value1 = (u16)tmpreg;
	*value2 = (u16)(tmpreg >> 16);
}

static inline void __amba_write2w(volatile void __iomem *address,
	u16 value1, u16 value2)
{
	volatile void __iomem *base;
	u32 tmpreg;

	BUG_ON((u32)address & 0x03);

	base = (volatile void __iomem *)((u32)address & 0xFFFFFFFC);
	tmpreg = value2;
	tmpreg <<= 16;
	tmpreg |= value1;
	amba_writel(base, tmpreg);
}

#define amba_read2w(v,pd1,pd2)	__amba_read2w(v,pd1,pd2)
#define amba_write2w(v,d1,d2)	__amba_write2w(v,d1,d2)

#define amba_setbitsb(v, mask)	amba_writeb((v),(amba_readb(v) | (mask)))
#define amba_setbitsw(v, mask)	amba_writew((v),(amba_readw(v) | (mask)))
#define amba_setbitsl(v, mask)	amba_writel((v),(amba_readl(v) | (mask)))

#define amba_clrbitsb(v, mask)	amba_writeb((v),(amba_readb(v) & ~(mask)))
#define amba_clrbitsw(v, mask)	amba_writew((v),(amba_readw(v) & ~(mask)))
#define amba_clrbitsl(v, mask)	amba_writel((v),(amba_readl(v) & ~(mask)))

#define amba_tstbitsb(v, mask)	(amba_readb(v) & (mask))
#define amba_tstbitsw(v, mask)	(amba_readw(v) & (mask))
#define amba_tstbitsl(v, mask)	(amba_readl(v) & (mask))


/* ==========================================================================*/

#define amba_rct_readl(v)		__raw_readl((volatile void __iomem *)v)
#define amba_rct_writel(v,d)		__raw_writel(d, (volatile void __iomem *)v)
#define amba_rct_setbitsl(v, mask)	amba_rct_writel((v),(amba_rct_readl(v) | (mask)))
#define amba_rct_clrbitsl(v, mask)	amba_rct_writel((v),(amba_rct_readl(v) & ~(mask)))

#endif /* __ASSEMBLER__ */
/* ==========================================================================*/

#endif

