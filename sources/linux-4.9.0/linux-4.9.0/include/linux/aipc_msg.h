/**
  * Copyright (c) 2014 by Ambarella Inc.

  * Permission is hereby granted, free of charge, to any person obtaining a copy
  * of this software and associated documentation files (the "Software"), to deal
  * in the Software without restriction, including without limitation the rights
  * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  * copies of the Software, and to permit persons to whom the Software is
  * furnished to do so, subject to the following conditions:

  * The above copyright notice and this permission notice shall be included in
  * all copies or substantial portions of the Software.

  * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  * THE SOFTWARE.

  * aipc_msg.h: defines AIPC message format shared by all hosts
  *
  * Authors: Joey Li <jli@ambarella.com>
  *
 **/
#ifndef __AIPC_MSG_H__
#define __AIPC_MSG_H__

#include "AmbaIPC_Rpc_Def.h"

/**
 * The defintion for rpc client ID
 *
 * @see AmbaIPC_ClientCreate
 */
typedef void* CLIENT_ID_t;

#define AIPC_HOST_LINUX                 AMBA_IPC_HOST_LINUX
#define AIPC_HOST_THREADX               AMBA_IPC_HOST_THREADX
#define AIPC_HOST_MAX                   AMBA_IPC_HOST_MAX

#define AIPC_BINDING_PORT               111
#define AIPC_CLNT_CONTROL_PORT          112
#define AIPC_CLIENT_NR_MAX              8

typedef enum _AMBA_IPC_BINDER_e_ {
	AMBA_IPC_BINDER_BIND = 0,
	AMBA_IPC_BINDER_REGISTER,
	AMBA_IPC_BINDER_UNREGISTER,
	AMBA_IPC_BINDER_LIST,
	AMBA_IPC_BINDER_REBIND,
	AMBA_IPC_BINDER_FIND
} AMBA_IPC_BINDER_e;

typedef enum _AMBA_IPC_MSG_e_ {
	AMBA_IPC_MSG_CALL = 0,
	AMBA_IPC_MSG_REPLY
} AMBA_IPC_MSG_e;

#define AIPC_MSG_CALL                   AMBA_IPC_MSG_CALL
#define AIPC_MSG_REPLY                  AMBA_IPC_MSG_REPLY

#define AIPC_REPLY_SUCCESS              AMBA_IPC_REPLY_SUCCESS
#define AIPC_REPLY_PROG_UNAVAIL         AMBA_IPC_REPLY_PROG_UNAVAIL
#define AIPC_REPLY_PARA_INVALID         AMBA_IPC_REPLY_PARA_INVALID
#define AIPC_REPLY_SYSTEM_ERROR         AMBA_IPC_REPLY_SYSTEM_ERROR

/*
 * if RPC_DEBUG is on, please also enable RPMSG_DEBUG.
 * RPMSG_DEBUG is defined in plat/remoteproc.h
 */
#define RPC_DEBUG

struct aipc_msg_call {
	int  prog;              /* program number   */
	int  vers;              /* version number   */
	int  proc;              /* procedure number */
};

struct aipc_msg_reply {
	AMBA_IPC_REPLY_STATUS_e  status;            /* reply status     */
};

struct aipc_msg {
	int  type;              /* body type: call/reply */
	union {
		struct aipc_msg_call    call;
		struct aipc_msg_reply   reply;
	} u;
	unsigned long  parameters[0];
};

struct aipc_xprt {
	unsigned char   client_addr;      /* client address */
	unsigned char   server_addr;      /* server address */
	unsigned int	xid;              /* transaction ID */
	unsigned int    client_port;      /* client port    */
	unsigned int	client_ctrl_port; /* client control port */
	unsigned int    server_port;      /* server port    */
	unsigned int	mode;		  /* communication mode*/
	unsigned long   private;
#ifdef RPC_DEBUG
	/* RPC profiling in ThreadX side */
	unsigned int	tx_rpc_send_start;
	unsigned int	tx_rpc_send_end;
	unsigned int	tx_rpc_recv_start;
	unsigned int	tx_rpc_recv_end;
	/* RPC profiling in Linux */
	unsigned int	lk_to_lu_start;
	unsigned int	lk_to_lu_end;
	unsigned int	lu_to_lk_start;
	unsigned int	lu_to_lk_end;
#endif
};

struct aipc_pkt {
	struct aipc_xprt xprt;
	struct aipc_msg  msg;
};

#define AIPC_HDRLEN     ((sizeof(struct aipc_pkt)+3)&~3)

#endif //__AIPC_MSG_H__

