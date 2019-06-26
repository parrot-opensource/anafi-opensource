/*
 * arch/arm/plat-ambarella/misc/aipc_binder.c
 *
 * Author: Joey Li <jli@ambarella.com>
 *
 * Copyright (C) 2013, Ambarella, Inc.
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
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <net/sock.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/aipc_msg.h>
#include "aipc_priv.h"

struct xprt_ops xprt_func[AIPC_HOST_MAX];

/*
 * register the xprt for a host
 */
void aipc_router_register_xprt(unsigned int host, struct xprt_ops *ops)
{
	if (host < AIPC_HOST_MAX) {
		memcpy(&xprt_func[host], ops, sizeof(struct xprt_ops));
	} else {
		EMSG("host id %d is out of range\n", host);
	}
}

/*
 * send the msg to its target
 */
void aipc_router_send(struct aipc_pkt *pkt, int len)
{
	int idx;
	unsigned int port;

	DMSG("aipc_binder_send: (%c %u), (%c %u)\n",
		pkt->xprt.server_addr, pkt->xprt.server_port,
		pkt->xprt.client_addr, pkt->xprt.client_port);
	if (pkt->msg.type == AIPC_MSG_CALL) {
		idx  = pkt->xprt.server_addr;
		port = pkt->xprt.server_port;
	} else {
		idx  = pkt->xprt.client_addr;
		port = pkt->xprt.client_port;
	}

	if (idx < AIPC_HOST_MAX && xprt_func[idx].send && port) {
		xprt_func[idx].send(pkt, len, port);
	} else {
		printk(KERN_ERR "aipc_router xprt error: host=%d, port=%u",
			idx, port);
	}
}

static int __init aipc_init(void)
{
	aipc_nl_init();
	return 0;
}

static void __exit aipc_exit(void)
{
	aipc_nl_exit();
}

module_init(aipc_init);
module_exit(aipc_exit);
MODULE_LICENSE("GPL");

