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
 **/
#ifndef __AMBAIPC_RPC_DEF_H__
#define __AMBAIPC_RPC_DEF_H__

typedef enum _AMBA_IPC_HOST_e_ {
	AMBA_IPC_HOST_LINUX = 0,
	AMBA_IPC_HOST_THREADX,
	AMBA_IPC_HOST_MAX
} AMBA_IPC_HOST_e;

typedef enum _AMBA_IPC_REPLY_STATUS_e_ {
	AMBA_IPC_REPLY_SUCCESS = 0,
	AMBA_IPC_REPLY_PROG_UNAVAIL,
	AMBA_IPC_REPLY_PARA_INVALID,
	AMBA_IPC_REPLY_SYSTEM_ERROR,
	AMBA_IPC_REPLY_TIMEOUT,
	AMBA_IPC_INVALID_CLIENT_ID,
	AMBA_IPC_UNREGISTERED_SERVER,
	AMBA_IPC_REREGISTERED_SERVER,
	AMBA_IPC_SERVER_MEM_ALLOC_FAILED,
	AMBA_IPC_IS_NOT_READY,
	AMBA_IPC_CRC_ERROR,
	AMBA_IPC_NETLINK_ERROR,
	AMBA_IPC_STATUS_NUM,
	AMBA_IPC_REPLY_MAX = 0xFFFFFFFF
} AMBA_IPC_REPLY_STATUS_e;

typedef enum _AMBA_IPC_COMMUICATION_MODE_e_ {
	AMBA_IPC_SYNCHRONOUS = 0,
	AMBA_IPC_ASYNCHRONOUS,
	AMBA_IPC_MODE_MAX = 0xFFFFFFFF
} AMBA_IPC_COMMUICATION_MODE_e ;

typedef struct _AMBA_IPC_SVC_RESULT_s_ {
	int Length;
	void *pResult;
	AMBA_IPC_COMMUICATION_MODE_e Mode;
	AMBA_IPC_REPLY_STATUS_e Status;
} AMBA_IPC_SVC_RESULT_s;

/* function pointer prototype for svc procedure */
typedef void (*AMBA_IPC_PROC_f)(void *, AMBA_IPC_SVC_RESULT_s *);

typedef struct _AMBA_IPC_PROC_s_ {
    AMBA_IPC_PROC_f Proc;
    AMBA_IPC_COMMUICATION_MODE_e Mode;
} AMBA_IPC_PROC_s;

typedef struct _AMBA_IPC_PROG_INFO_s_ {
	int ProcNum;
	AMBA_IPC_PROC_s *pProcInfo;
} AMBA_IPC_PROG_INFO_s;
#endif