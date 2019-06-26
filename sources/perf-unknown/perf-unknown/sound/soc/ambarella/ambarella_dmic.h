/*
 * sound/soc/ambarella_i2s.h
 *
 * History:
 *	2016/07/13 - [XianqingZheng] created file
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

#ifndef AMBARELLA_DMIC_H_
#define AMBARELLA_DMIC_H_

struct amb_dmic_priv {
	void __iomem 		*regbase;
	struct regmap		*reg_scr;
	u32 			mclk;
};

#endif /*AMBARELLA_I2S_H_*/

