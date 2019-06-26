/*
 * Copyright (C) 2018 Parrot Drones SAS
 */
#ifndef __PGIMBAL_INTERFACE_H__
#define __PGIMBAL_INTERFACE_H__

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

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
	PGIMBAL_RPMSG_TYPE_CALIBRATION_STATE,		/* TX out - LX in */
	PGIMBAL_RPMSG_TYPE_CALIBRATION_RESULT,		/* TX out - LX in */
	PGIMBAL_RPMSG_TYPE_ALERTS,			/* TX out - LX in */
	PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STARTED,	/* TX in - LX out */
	PGIMBAL_RPMSG_TYPE_OFFSETS_UPDATE_STOPPED,	/* TX in - LX out */
	PGIMBAL_RPMSG_TYPE_OFFSET,			/* TX and LX in/out */

	PGIMBAL_RPMSG_TYPE_COUNT,
	PGIMBAL_RPMSG_TYPE_FORCE_ENUM = 0xffffffff,
};

enum pgimbal_calibration_state {
	PGIMBAL_CALIBRATION_STATE_REQUIRED = 0,
	PGIMBAL_CALIBRATION_STATE_IN_PROGRESS,
	PGIMBAL_CALIBRATION_STATE_OK,

	PGIMBAL_CALIBRATION_STATE_COUNT,
	PGIMBAL_CALIBRATION_STATE_ENUM = 0xffffffff
};

enum pgimbal_calibration_result {
	PGIMBAL_CALIBRATION_RESULT_SUCCEEDED,
	PGIMBAL_CALIBRATION_RESULT_FAILED,
	PGIMBAL_CALIBRATION_RESULT_CANCELED,

	PGIMBAL_CALIBRATION_RESULT_COUNT,
	PGIMBAL_CALIBRATION_RESULT_ENUM = 0xffffffff
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

enum pgimbal_alerts_type {
	PGIMBAL_ALERT_TYPE_CALIBRATION = (1 << 0),
	PGIMBAL_ALERT_TYPE_OVERLOAD = (1 << 1),
	PGIMBAL_ALERT_TYPE_COMMUNICATION = (1 << 2),
	PGIMBAL_ALERT_TYPE_CRITICAL = (1 << 3),
};

struct pgimbal_rpmsg {
	enum pgimbal_rpmsg_type type;
	union {
		enum pgimbal_calibration_state calibration_state;
		enum pgimbal_calibration_result calibration_result;
		struct pgimbal_offset_info offset_info;
		uint8_t alerts;
	};
};

#endif /* __PGIMBAL_INTERFACE_H__ */
