/*
 * Copyright (C) 2017 Parrot S.A.
 *     Author: Aurelien Lefebvre <aurelien.lefebvre@parrot.com>
 */
#ifndef __PBT_INTERFACE_H__
#define __PBT_INTERFACE_H__

#define PBT_RPMSG_CHANNEL "pbt"

enum pbt_rpmsg_type {
	PBT_RPMSG_TYPE_INVALID = 0,

	PBT_RPMSG_TYPE_MEM_INFO,
	PBT_RPMSG_TYPE_PING,
	PBT_RPMSG_TYPE_PONG,

	PBT_RPMSG_TYPE_COUNT,
	PBT_RPMSG_TYPE_FORCE_ENUM = 0xffffffff,
};

struct pbt_meminfo {
	int32_t size;
	uint64_t addr;
};

struct pbt_rpmsg {
	enum pbt_rpmsg_type type;
	union {
		int32_t value;
		struct pbt_meminfo meminfo;
	};
};

/* Address translater (32 <-> 64 bits) */
static inline void pbt_set_addr(struct pbt_meminfo *meminfo, uintptr_t addr)
{
#if __SIZEOF_POINTER__ == 8
	meminfo->addr = (uint64_t) addr;
#else
	meminfo->addr = (uint64_t) (uint32_t) addr;
#endif
}

static inline uintptr_t pbt_get_addr(struct pbt_meminfo *meminfo)
{
#if __SIZEOF_POINTER__ == 8
	return (uintptr_t) meminfo->addr;
#else
	return (uintptr_t) (uint32_t) meminfo->addr;
#endif
}

#endif /* __PBT_INTERFACE_H__ */
