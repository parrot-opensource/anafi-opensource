/*
 * Copyright (C) 2018 Parrot Drones SAS
 */
#ifndef __PGIMBAL_INTERFACE_H__
#define __PGIMBAL_INTERFACE_H__

#define _XOPEN_SOURCE 500

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#include <math.h>
#endif

#define PGIMBAL_RPMSG_CHANNEL	"pgimbal"
#define PGIMBAL_INCREMENT	(10) /* 0.1 [deg] */

enum pgimbal_rpmsg_type {
	PGIMBAL_RPMSG_TYPE_INVALID = 0,

	PGIMBAL_RPMSG_TYPE_CALIBRATION_REQUEST,		/* TX in - LX out */
	PGIMBAL_RPMSG_TYPE_ALERT,			/* TX out - LX in */
	PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STARTED,	/* TX in - LX out */
	PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STOPPED,	/* TX in - LX out */
	PGIMBAL_RPMSG_TYPE_OFFSET,			/* TX and LX in/out */

	PGIMBAL_RPMSG_TYPE_COUNT,
	PGIMBAL_RPMSG_TYPE_FORCE_ENUM = 0xffffffff,
};

enum pgimbal_axis {
	PGIMBAL_AXIS_X = 0,
	PGIMBAL_AXIS_Y,
	PGIMBAL_AXIS_Z,

	PGIMBAL_AXIS_COUNT,
	PGIMBAL_AXIS_FORCE_ENUM = 0xffffffff
};

struct pgimbal_offset_info {
	enum pgimbal_axis id;
	int offset;
};

struct pgimbal_rpmsg {
	enum pgimbal_rpmsg_type type;
	union {
		struct pgimbal_offset_info offset_info;
	};
};

#endif /* __PGIMBAL_INTERFACE_H__ */
