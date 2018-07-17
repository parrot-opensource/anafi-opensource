/*
 * arch/arm/plat-ambarella/include/plat/remoteproc.h
 *
 * Author: Tzu-Jung Lee <tjlee@ambarella.com>
 *
 * Copyright (C) 2012-2012, Ambarella, Inc.
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

#ifndef __PLAT_AMBARELLA_REMOTEPROC_H
#define __PLAT_AMBARELLA_REMOTEPROC_H

#include <linux/remoteproc.h>

/*
 * The rpmsg profiling related data structure use the same definition in dual-OSes.
 * This data structure
 */
//#define RPMSG_DEBUG

struct ambarella_rproc_pdata {
	const char              *name;
	struct rproc            *rproc;
	const char              *firmware;
	unsigned int            svq_tx_irq;
	unsigned int            svq_rx_irq;
	unsigned int            rvq_tx_irq;
	unsigned int            rvq_rx_irq;
	const struct rproc_ops  *ops;
	unsigned long           buf_addr_pa;
	struct work_struct      svq_work;
	struct work_struct      rvq_work;
	struct resource_table   *(*gen_rsc_table)(int *tablesz);
        struct regmap           *reg_ahb_scr;
};

#ifdef RPMSG_DEBUG
/******************************The defintion is shared between dual-OSes*******************************/
typedef struct {
	unsigned int ToGetSvqBuffer;
	unsigned int GetSvqBuffer;
	unsigned int SvqToSendInterrupt;
	unsigned int SvqSendInterrupt;
} AMBA_RPMSG_PROFILE_s;

typedef struct _AMBA_RPMSG_STATISTIC_s_ {
	/**********************************ThreadX side**************************/
	unsigned int TxLastInjectTime;
	unsigned int TxTotalInjectTime;
	unsigned int TxSendRpmsgTime;
	unsigned int TxResponseTime;
	unsigned int MaxTxResponseTime;
	unsigned int TxRecvRpmsgTime;
	unsigned int TxRecvCallBackTime;
	unsigned int TxReleaseVqTime;
	unsigned int TxToLxRpmsgTime;
	unsigned int MaxTxToLxRpmsgTime;
	unsigned int MinTxToLxRpmsgTime;
	unsigned int MaxTxRecvCBTime;
	unsigned int MinTxRecvCBTime;
	int TxToLxCount;
	int TxToLxWakeUpCount;
	/************************************************************************/
	/**********************************Linux side****************************/
	unsigned int LxLastInjectTime;
	unsigned int LxTotalInjectTime;
	unsigned int LxSendRpmsgTime;
	unsigned int LxResponseTime;
	unsigned int MaxLxResponseTime;
	unsigned int LxRecvRpmsgTime;
	unsigned int LxRecvCallBackTime;
	unsigned int LxReleaseVqTime;
	unsigned int LxToTxRpmsgTime;
	unsigned int MaxLxToTxRpmsgTime;
	unsigned int MinLxToTxRpmsgTime;
	unsigned int MaxLxRecvCBTime;
	unsigned int MinLxRecvCBTime;
	int LxRvqIsrCount;
	int LxToTxCount;
	/************************************************************************/
} AMBA_RPMSG_STATISTIC_s;
/*******************************************************************************************************/

struct profile_data{
	unsigned int ToGetSvqBuffer;
	unsigned int GetSvqBuffer;
	unsigned int SvqToSendInterrupt;
	unsigned int SvqSendInterrupt;
	unsigned int ToGetRvqBuffer;
	unsigned int GetRvqBuffer;
	unsigned int ToRecvData;
	unsigned int RecvData;
	unsigned int ReleaseRvq;
	unsigned int RvqToSendInterrupt;
	unsigned int RvqSendInterrupt;
} ;
#endif /* The data structure of RPMSG profiling. */

#endif /* __PLAT_AMBARELLA_REMOTEPROC_H */
