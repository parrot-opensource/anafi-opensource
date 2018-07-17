/*
 * include/linux/aipc/rpmsg_sd.h
 *
 * Authors:
 *	Kerson Chen <cychenc@ambarella.com>
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
 * Copyright (C) 2011-2015, Ambarella Inc.
 */

#ifndef __AIPC_RPMSG_SD_H__
#define __AIPC_RPMSG_SD_H__

#ifdef __KERNEL__

typedef enum _AMBA_RPDEV_SD_CMD_e_ {
	SD_INFO_GET = 0,
	SD_RESP_GET,
	SD_DETECT_INSERT,
	SD_DETECT_EJECT,
	SD_DETECT_CHANGE,
	SD_RPMSG_ACK
} AMBA_RPDEV_SD_CMD_e;

struct rpdev_sdinfo {
	u32 host_id;	/**< from LK */
	u32 from_rpmsg : 1,
	    is_init : 1,
	    is_sdmem : 1,
	    is_mmc : 1,
	    is_sdio : 1,
	    rsv : 27;

	u16 bus_width;
	u16 hcs;
	u32 rca;
	u32 ocr;
	u32 clk;
} __attribute__((aligned(64), packed));

struct rpdev_sdresp {
	u32 host_id;
	u32 opcode;
	int ret;
	u32 padding;
	u32 resp[4];
	char buf[512];
} __attribute__((aligned(64), packed));

int rpmsg_sdinfo_get(void *data);
int rpmsg_sdresp_get(void *data);
int rpmsg_sd_detect_insert(u32 slot_id);
int rpmsg_sd_detect_eject(u32 slot_id);

#endif	/* __KERNEL__ */

#endif
