/*
 * arch/arm/plat-ambarella/misc/aipc_nl.c
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

struct sock *nl_sock = NULL;
extern struct xprt_ops xprt_func[AIPC_HOST_MAX];

/*
 * forward incoming msg to binder
 */
static void data_handler(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct aipc_pkt *pkt;
	int len;

	nlh = (struct nlmsghdr*)skb->data;
	pkt = (struct aipc_pkt*)NLMSG_DATA(nlh);
	len = NLMSG_PAYLOAD(nlh, 0);
	aipc_router_send(pkt, len);
}

static void aipc_nl_send(struct aipc_pkt *pkt, int len, int port)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int err, idx;

	DMSG("aipc_nl_send to port %u, len %d\n", port, len);
	skb = nlmsg_new(len, 0);
	nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, len, 0);
	NETLINK_CB(skb).dst_group = 0;
	memcpy(nlmsg_data(nlh), pkt, len);
	err = nlmsg_unicast(nl_sock, skb, port);
	/* we dont't need call nlmsg_free, kernel will take care of this */

	/* We only handle the wrong svc port case.
	 * The client port does not be allowed to change.
	 * If the reply pkt occurs netlink failed, this pkt is dropped.
	 */
	if(err && (pkt->msg.type == AIPC_MSG_CALL)) {
		/* To check the svc port with binder */
		pkt->xprt.server_port = AIPC_BINDING_PORT;
		pkt->xprt.private = pkt->msg.u.call.proc; /* temporarily record the original proc */
		pkt->msg.u.call.proc = AMBA_IPC_BINDER_REBIND;
		skb = nlmsg_new(len, 0);
		nlh = nlmsg_put(skb, 0, 0, NLMSG_DONE, len, 0);
		NETLINK_CB(skb).dst_group = 0;
		memcpy(nlmsg_data(nlh), pkt, len);
		err = nlmsg_unicast(nl_sock, skb, pkt->xprt.server_port); /* send it to ipcbind */

		if(err) {
			if(pkt->xprt.mode == AMBA_IPC_SYNCHRONOUS) {
				/*netlink really failed, but it is returned when sync client call */
				pkt->msg.type = AIPC_MSG_REPLY;
				pkt->msg.u.reply.status = AMBA_IPC_NETLINK_ERROR;
				idx = pkt->xprt.client_addr;
				port = pkt->xprt.client_port;
				if (idx < AIPC_HOST_MAX && xprt_func[idx].send && port) {
					xprt_func[idx].send(pkt, len, port);
				} else {
					 printk(KERN_ERR "%s aipc_router xprt error: host=%d, port=%u",
					 __func__, idx, port);
				}
			}
			else {
				printk("[warning] %s: netlink transmission failed. Please check the port info and ipcbinder.\n", __func__);
			}
		}
	}
}


int __init aipc_nl_init(void)
{
	struct netlink_kernel_cfg cfg = {
		.input = data_handler,
	};
	struct xprt_ops ops = {
		.send = aipc_nl_send,
	};
	nl_sock = netlink_kernel_create(&init_net, NL_PROTO_AMBAIPC, &cfg);
	aipc_router_register_xprt(AIPC_HOST_LINUX, &ops);
	return 0;
}

void __exit aipc_nl_exit(void)
{
	netlink_kernel_release(nl_sock);
}

