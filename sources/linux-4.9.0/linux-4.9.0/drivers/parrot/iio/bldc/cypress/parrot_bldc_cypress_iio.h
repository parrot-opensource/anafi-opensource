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
#ifndef __IIO_PARROT_BLDC_CYPRESS_H__
#define __IIO_PARROT_BLDC_CYPRESS_H__

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

/* FW version string maximum size */
#define FW_VERSION_MAX_SIZE 10

/**
 * struct bldc_cypress_platform_data - Platform data for the bldc
 * cypress driver
 *
 * @lut: Motors look up table ex: {0, 1, 2, 3}
 * @spin_dir: Motors spin direction ex: 0b1010, which is CCW/CW/CCW/CW
 */
struct bldc_cypress_platform_data {
	uint32_t	lut[4];
	uint32_t	spin_dir;
	int		gpio_reset;
};

struct bldc_transfer_function {
	int (*read_multiple_byte) (struct device *dev,
					u8 reg_addr,
					int len,
					u8 *data,
					int with_dummy_byte);
	int (*write_multiple_byte) (struct device *dev,
					u8 reg_addr,
					int len,
					u8 *data);
};

struct bldc_state {
	struct device *dev;
	struct iio_trigger  *trig;
	struct iio_chan_spec channels[PARROT_BLDC_SCAN_MAX];
	const struct bldc_transfer_function *tf;
	struct bldc_cypress_platform_data pdata;
	u8 *buffer;
	int is_overflow;
	char fw_version[FW_VERSION_MAX_SIZE];
	u8 hw_version;

	/* current sound state
	 * 0: no sound is currently playing
	 * n: sound n is currently playing */
	u8 sound;
};

int bldc_cypress_probe(struct iio_dev *indio_dev);
void bldc_cypress_remove(struct iio_dev *indio_dev);
irqreturn_t bldc_cypress_read_fifo(int irq, void *p);
int bldc_cypress_fetch_data(struct iio_dev *indio_dev);

#endif
