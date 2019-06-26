/*
 * arch/arm/plat-ambarella/include/plat/fb.h
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

#ifndef __PLAT_AMBARELLA_FB_H
#define __PLAT_AMBARELLA_FB_H

#ifndef __ASSEMBLER__

#define AMBFB_IS_PRIVATE_EVENT(evt)		(((evt) >> 16) == 0x5252)
#define AMBFB_EVENT_OPEN			0x52520001
#define AMBFB_EVENT_RELEASE			0x52520002
#define AMBFB_EVENT_CHECK_PAR			0x52520003
#define AMBFB_EVENT_SET_PAR			0x52520004
#define AMBFB_EVENT_PAN_DISPLAY			0x52520005

#endif /* __ASSEMBLER__ */

#endif

