/*
 * arch/arm/plat-ambarella/misc/aipc_priv.h
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
#ifndef __AIPC_PRIV_H__
#define __AIPC_PRIV_H__

#define NL_PROTO_AMBAIPC     25

#define EMSG(format, ...)			\
	do {					\
		printk(format, ##__VA_ARGS__);	\
	} while (0)

#define DEBUG_AIPC_FLOW		0
	
#if DEBUG_AIPC_FLOW
#define DMSG EMSG
#else
#define DMSG(...)
#endif


struct aipc_pkt;

struct xprt_ops {
	void (*send)(struct aipc_pkt*, int, int);
};

void aipc_router_register_xprt(unsigned int host, struct xprt_ops *ops); 

void aipc_router_send(struct aipc_pkt *pkt, int len);

int  aipc_nl_init(void);

void aipc_nl_exit(void);

#endif //__AIPC_PRIV_H__
