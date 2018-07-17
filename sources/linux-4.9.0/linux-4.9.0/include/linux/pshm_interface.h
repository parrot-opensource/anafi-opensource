/*
 * Copyright (C) 2016 Parrot S.A.
 *     Author: Aurelien Lefebvre <aurelien.lefebvre@parrot.com>
 */
#ifndef __PSHM_INTERFACE_H__
#define __PSHM_INTERFACE_H__

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define PSHM_RPMSG_CHANNEL "pshm"

#define PSHM_NAME_MAX_LENGTH 32

#define PSHM_IOCTL_NUM '}'
#define PSHM_IOCTL_CREATE _IOWR(PSHM_IOCTL_NUM, 1, struct pshm_meminfo)
#define PSHM_IOCTL_GET _IOR(PSHM_IOCTL_NUM, 2, struct pshm_meminfo)
#define PSHM_IOCTL_CREATE_2 _IOWR(PSHM_IOCTL_NUM, 3, struct pshm_meminfo)

enum pshm_rpmsg_type {
	PSHM_RPMSG_TYPE_INVALID = 0,

	PSHM_RPMSG_TYPE_CREATE,          /* TX in / LX out */
	PSHM_RPMSG_TYPE_GET,             /* TX in / LX out */
	PSHM_RPMSG_TYPE_MEMINFO,         /* TX out / LX in */
	PSHM_RPMSG_TYPE_FAILED,          /* TX out / LX in */

	PSHM_RPMSG_TYPE_COUNT,
	PSHM_RPMSG_TYPE_FORCE_ENUM = 0xffffffff,
};

enum pshm_cache_mode {
	PSHM_CACHE_MODE_NOT_CACHEABLE,
	PSHM_CACHE_MODE_CACHEABLE,

	PSHM_CACHE_MODE_COUNT,
	PSHM_CACHE_MODE_FORCE_ENUM = 0xffffffff,
};

struct pshm_meminfo {
	char name[PSHM_NAME_MAX_LENGTH];
	uint32_t size;
	uint64_t addr;
	uint8_t new_alloc;
	enum pshm_cache_mode cache;
};

struct pshm_rpmsg {
	enum pshm_rpmsg_type type;
	union {
		struct pshm_meminfo meminfo;
		int32_t err; /* failed case */
	};
};

/* Address translater (32 <-> 64 bits) */
static inline void pshm_set_addr(struct pshm_meminfo *meminfo, uintptr_t addr)
{
#if __SIZEOF_POINTER__ == 8
	meminfo->addr = (uint64_t) addr;
#else
	meminfo->addr = (uint64_t) (uint32_t) addr;
#endif
}

static inline uintptr_t pshm_get_addr(struct pshm_meminfo *meminfo)
{
#if __SIZEOF_POINTER__ == 8
	return (uintptr_t) meminfo->addr;
#else
	return (uintptr_t) (uint32_t) meminfo->addr;
#endif
}

#endif /* __PSHM_INTERFACE_H__ */
