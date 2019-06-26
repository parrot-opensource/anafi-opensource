/*
 * include/misc/amba_dspmem.h
 *
 * Copyright 2014 Ambarella Inc.
 *
 */

#ifndef _MISC_AMBA_DSPMEM_H
#define _MISC_AMBA_DSPMEM_H

#include <linux/ioctl.h>

#define AMBA_DSPMEM_NAME_DEF		"/dev/amba_dspmem"

struct AMBA_DSPMEM_INFO_s {
	unsigned long long base;
	unsigned long long phys;
	unsigned int       size;
};

#define __AMBA_DSPMEM		0x99

#define AMBA_DSPMEM_GET_INFO	_IOR(__AMBA_DSPMEM, 1, struct AMBA_DSPMEM_INFO_s)

#endif	/* _MISC_AMBA_DSPMEM_H */
