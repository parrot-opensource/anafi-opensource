/*
 * include/linux/aipc/ipc_mutex.h
 *
 * Authors:
 *	Joey Li <jli@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2013-2015, Ambarella Inc.
 */

#ifndef __AIPC_MUTEX_H__
#define __AIPC_MUTEX_H__

/*---------------------------------------------------------------------------*\
 *  * AmbaIPC global mutex and spinlock related APIs
\*---------------------------------------------------------------------------*/
typedef enum _AMBA_IPC_MUTEX_IDX_e_ {
	/* IDC master instances */
	AMBA_IPC_MUTEX_I2C_CHANNEL0 = 0,
	AMBA_IPC_MUTEX_I2C_CHANNEL1,
	AMBA_IPC_MUTEX_I2C_CHANNEL2,

	AMBA_IPC_MUTEX_SPI_CHANNEL0,
	AMBA_IPC_MUTEX_SPI_CHANNEL1,
	AMBA_IPC_MUTEX_SPI_CHANNEL2,

	AMBA_IPC_MUTEX_SD0,
	AMBA_IPC_MUTEX_SD1,
	AMBA_IPC_MUTEX_SD2,
	AMBA_IPC_MUTEX_NAND,

	AMBA_IPC_MUTEX_GPIO,

	AMBA_IPC_MUTEX_RCT,

	AMBA_IPC_MUTEX_SPINOR,

	AMBA_IPC_NUM_MUTEX      /* Total number of global mutex */
} AMBA_IPC_MUTEX_IDX_e;

void aipc_mutex_lock(int id);
void aipc_mutex_unlock(int id);

#endif	/* __AIPC_MUTEX_H__ */

