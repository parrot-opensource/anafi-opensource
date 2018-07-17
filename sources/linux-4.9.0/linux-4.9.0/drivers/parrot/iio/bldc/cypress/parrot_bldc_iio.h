/**
 ************************************************
 * @file bldc_cypress_core.c
 * @brief BLDC cypress IIO driver
 *
 * Copyright (C) 2015 Parrot S.A.
 *
 * @author Karl Leplat <karl.leplat@parrot.com>
 * @date 2015-06-22
 *************************************************
 */
#ifndef __IIO_PARROT_BLDC_H__
#define __IIO_PARROT_BLDC_H__

#include <linux/i2c.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/kfifo_buf.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/i2c.h>

#define PARROT_BLDC_REG_REF_SPEED	2
#define PARROT_BLDC_REG_OBS_DATA	32
#define PARROT_BLDC_REG_START_PROP	64
#define PARROT_BLDC_REG_STOP_PROP	96
#define PARROT_BLDC_REG_CLEAR_ERROR	128
#define PARROT_BLDC_REG_LED		77
#define PARROT_BLDC_REG_INFO		160
#define PARROT_BLDC_REG_CLEAR_INFO	250
#define PARROT_BLDC_REG_TEST_SOUND      130

#define PARROT_BLDC_GET_INFO_LENGTH	14
/* observation data length with pwm */
#define PARROT_BLDC_OBS_DATA_LENGTH	19

/* scan element definition */
enum {
	PARROT_BLDC_SCAN_OBS_DATA_SPEED_MOTOR1,
	PARROT_BLDC_SCAN_OBS_DATA_SPEED_MOTOR2,
	PARROT_BLDC_SCAN_OBS_DATA_SPEED_MOTOR3,
	PARROT_BLDC_SCAN_OBS_DATA_SPEED_MOTOR4,
	PARROT_BLDC_SCAN_OBS_DATA_BATT_VOLTAGE,
	PARROT_BLDC_SCAN_OBS_DATA_STATUS,
	PARROT_BLDC_SCAN_OBS_DATA_ERRNO,
	PARROT_BLDC_SCAN_OBS_DATA_FAULT_MOTOR,
	PARROT_BLDC_SCAN_OBS_DATA_TEMP,
	PARROT_BLDC_SCAN_TIMESTAMP,
	PARROT_BLDC_SCAN_MAX,
};

#define PARROT_BLDC_OBS_NAME_BATT_VOLTAGE	"batt"
#define PARROT_BLDC_OBS_NAME_STATUS		"status"
#define PARROT_BLDC_OBS_NAME_ERRNO		"errno"
#define PARROT_BLDC_OBS_NAME_FAULT_MOTOR	"fm"
#define PARROT_BLDC_OBS_NAME_TEMP		"temp"

#define PARROT_BLDC_OBS_CHAN_EXT_NAME(_mod, _channel, _type, _bits, _storage)\
{									\
	.type = _type,							\
	.modified = 0,							\
	.extend_name = PARROT_BLDC_OBS_NAME_ ## _mod,			\
	.channel =  _channel,						\
	.scan_index = PARROT_BLDC_SCAN_OBS_DATA_ ## _mod,		\
	.indexed = 1,							\
	.scan_type = {							\
		.sign = 'u',						\
		.realbits = (_bits),					\
		.storagebits = (_storage),				\
		.shift = 0,						\
		.endianness = IIO_BE,					\
	},								\
}

#define PARROT_BLDC_OBS_CHAN(_mod, _channel, _type, _bits, _storage)	\
{									\
	.type = _type,							\
	.modified = 0,							\
	.channel =  _channel,						\
	.scan_index = PARROT_BLDC_SCAN_OBS_DATA_ ## _mod,		\
	.indexed = 1,							\
	.scan_type = {							\
		.sign = 'u',						\
		.realbits = (_bits),					\
		.storagebits = (_storage),				\
		.shift = 0,						\
		.endianness = IIO_BE,					\
	},								\
}


/* IIO attribute address */
enum PARROT_BLDC_IIO_ATTR_ADDR {
	ATTR_START_MOTORS,
	ATTR_START_MOTORS_WITH_DIR,
	ATTR_STOP_MOTORS,
	ATTR_SPIN_DIRECTION_MOTORS,
	ATTR_LED,
	ATTR_REBOOT,
	ATTR_CLEAR_ERROR,
	ATTR_MOTORS_SPEED,
	ATTR_INFO_FT_GET,
	ATTR_INFO_CLEAR_FT,
	ATTR_INFO_SPIN_DIR,
	ATTR_OBS_MOTORS,
	ATTR_RC,
	ATTR_PLAY_SOUND,
	ATTR_PLAY_CUSTOM_SOUND,
};

#endif

