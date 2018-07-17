/**
 ************************************************
 * @file servo_messages.h
 * @brief BLDC active_semi IIO driver
 *
 * Copyright (C) 2015 Parrot S.A.
 *
 * @author Karl Leplat <karl.leplat@parrot.com>
 * @date 2015-06-22
 *************************************************
 */
#ifndef _SERVO_MESSAGES_H_
#define _SERVO_MESSAGES_H_

enum pm_motor_state {
	PM_MOTOR_STATE_INIT,
	PM_MOTOR_STATE_IDLE,
	PM_MOTOR_STATE_RAMPING,
	PM_MOTOR_STATE_CLOSED_LOOP,
	PM_MOTOR_STATE_STOPPING,
	PM_MOTOR_STATE_SUICIDED,

	/* last */
	PM_MOTOR_STATE_COUNT
};

enum pm_motor_error {
	/* Return by the FOC */
	PM_MOTOR_ERR_NONE,                      /* Nonimal case */
	PM_MOTOR_ERR_OTHER,                     /* */
	PM_MOTOR_ERR_UNDER_OVER_VOLTAGE,        /* Over/Under Voltage Fault */
	PM_MOTOR_ERR_MINMAX_SPEED,              /* Error low/high speed */
	PM_MOTOR_ERR_STALLED,                   /* Motor low speed detected */
	PM_MOTOR_ERR_SPEED_ANGL_DIF,            /* Angle and speed error is not within range */
	PM_MOTOR_ERR_HARD_STOP,                 /* Fault when braking */
	PM_MOTOR_ERR_CODE,                      /* Segfault, overflow */
	PM_MOTOR_ERR_CAFE,                      /* Fault with the Configurable Analog Front-End */
	PM_MOTOR_ERR_ARM,                       /* Cortex fault */
	PM_MOTOR_ERR_COMM_TIMEOUT,              /* Communication lost */
	PM_MOTOR_ERR_OPEN_PHASE_DETECTOR,       /* Detect if motor is plugged to the board */
	PM_MOTOR_ERR_ASSERT,                    /* Failure by assert() */
	PM_MOTOR_ERR_COMM,						/* Error communication */

	/* last */
	PM_MOTOR_ERR_COUNT
};

#ifdef __KERNEL__
/*
 * ProtoMotor high level API.
 */
enum motor_rotation {
	MOTOR_ROTATION_CLOCKWISE,
	MOTOR_ROTATION_COUNTERCLOCKWISE,
};

enum pm_motor_op {
	PM_MOTOR_OP_NOP,
	PM_MOTOR_OP_START,
	PM_MOTOR_OP_STOP,
	PM_MOTOR_OP_SET_SPEED,
	PM_MOTOR_OP_HW_RESET,

	/* last */
	PM_MOTOR_OP_COUNT
};

static const char * const pm_motor_error_msgs[PM_MOTOR_ERR_COUNT + 1U] = {
	[PM_MOTOR_ERR_NONE]                     = "No error",
	[PM_MOTOR_ERR_UNDER_OVER_VOLTAGE]       = "Over/Under Voltage Fault",
	[PM_MOTOR_ERR_MINMAX_SPEED]             = "Min/Max speed",
	[PM_MOTOR_ERR_STALLED]                  = "Motor stalled",
	[PM_MOTOR_ERR_SPEED_ANGL_DIF]           = "Angle and speed error is not within range",
	[PM_MOTOR_ERR_HARD_STOP]                = "Error when braking",
	[PM_MOTOR_ERR_COMM_TIMEOUT]             = "Communication lost",
	[PM_MOTOR_ERR_OPEN_PHASE_DETECTOR]      = "Motor not plugged to the ESC",
	[PM_MOTOR_ERR_ASSERT]                   = "Wrong assert",
	[PM_MOTOR_ERR_COMM]                     = "Communication error",
	[PM_MOTOR_ERR_CODE]                     = "FIX16 overflow",
	[PM_MOTOR_ERR_CAFE]                     = "CAFE fault",
	[PM_MOTOR_ERR_ARM]                      = "ARM fault",
	[PM_MOTOR_ERR_COUNT]                    = "Invalid last error code",
};

enum pm_proto_error {
	PM_PROTO_ERR_NONE = 0,

	PM_PROTO_ERR_ARG        = 0x01, /* Invalid arguments */
	PM_PROTO_ERR_ECC        = 0x02, /* ECC error */
	PM_PROTO_ERR_CMD        = 0x04, /* Invalid command */
	PM_PROTO_ERR_SPEED      = 0x08, /* Invalid speed */
	PM_PROTO_ERR_STATE      = 0x10, /* Invalid state */
	PM_PROTO_ERR_LAST_ERROR = 0x20, /* Invalid last error code */
};

static const char * const pm_proto_error_msgs[] = {
	[PM_PROTO_ERR_ARG]		= "Invalid arguments",
	[PM_PROTO_ERR_ECC]		= "ECC error",
	[PM_PROTO_ERR_CMD]		= "Invalid command",
	[PM_PROTO_ERR_SPEED]		= "Invalid speed",
	[PM_PROTO_ERR_STATE]		= "Invalid state",
	[PM_PROTO_ERR_LAST_ERROR]	= "Invalid last error code",
};

struct pm_motor_command {
	enum pm_motor_op op;   /* Op code */
	int speed;             /** RPM speed signed following trigonometric
				* conventions (positive is counterclockwise)
				*/
	bool clear_error;       /** Flag to request clearing last_error in
				 * statuses
				 */
	int leds_mask;
};

struct pm_motor_status {
	int speed;			/** RPM speed signed following
					 * trigonometric conventions (positive
					 * is counterclockwise)
					 */
	int state;			/* Motor state */
	int last_error;			/** Last detected error, persistent
					 * until clear_error flag is received
					 */
};

#endif /* __KERNEL__ */

#endif /* _SERVO_MESSAGES_H_ */
