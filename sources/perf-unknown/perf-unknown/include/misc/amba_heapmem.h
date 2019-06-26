/*
 * include/misc/amba_heapmem.h
 *
 * Copyright 2014 Ambarella Inc.
 *
 */

#ifndef _MISC_AMBA_HEAPMEM_H
#define _MISC_AMBA_HEAPMEM_H

#include <linux/ioctl.h>

#define AMBA_HEAPMEM_NAME_DEF		"/dev/amba_heapmem"

struct AMBA_HEAPMEM_INFO_s {
	unsigned long long base;
	unsigned long long phys;
	unsigned int       size;
};

#define __AMBA_HEAPMEM		0x99

#define AMBA_HEAPMEM_GET_INFO		_IOR(__AMBA_HEAPMEM, 1, struct AMBA_HEAPMEM_INFO_s)
#define AMBA_HEAPMEM_ACCESS_TEST 	_IOR(__AMBA_HEAPMEM, 2, unsigned long long)

#endif	/* _MISC_AMBA_HEAPMEM_H */
