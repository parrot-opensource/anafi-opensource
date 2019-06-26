/*
* Copyright (C) 2012 Invensense, Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/poll.h>
#include "inv_mpu_iio.h"

static void inv_clear_kfifo(struct inv_mpu6050_state *st)
{
	unsigned long flags;

	/* take the spin lock sem to avoid interrupt kick in */
	spin_lock_irqsave(&st->time_stamp_lock, flags);
	kfifo_reset(&st->timestamps);
	spin_unlock_irqrestore(&st->time_stamp_lock, flags);
}

static void inv_clear_trigger_counter(struct inv_mpu6050_state *st)
{
	unsigned long flags;

	/* take the spin lock sem to avoid interrupt kick in */
	spin_lock_irqsave(&st->trigger_lock, flags);
	st->trigger_counter = 0;
	spin_unlock_irqrestore(&st->trigger_lock, flags);
}

int inv_reset_fifo(struct iio_dev *indio_dev)
{
	int result;
	u8 d;
	struct inv_mpu6050_state  *st = iio_priv(indio_dev);

	/* disable interrupt */
	result = regmap_write(st->map, st->reg->int_enable, 0);
	if (result) {
		dev_err(regmap_get_device(st->map), "int_enable failed %d\n",
			result);
		return result;
	}
	/* disable the sensor output to FIFO */
	result = regmap_write(st->map, st->reg->fifo_en, 0);
	if (result)
		goto reset_fifo_fail;

	/* disable FIFO */
	result = regmap_write(st->map, st->reg->user_ctrl, 0);
	if (result)
		goto reset_fifo_fail;

	/* reset FIFO*/
	result = regmap_write(st->map, st->reg->user_ctrl,
			      INV_MPU6050_BIT_FIFO_RST);
	if (result)
		goto reset_fifo_fail;

	/* clear timestamps fifo */
	inv_clear_kfifo(st);
	inv_clear_trigger_counter(st);

	/* enable FIFO */
	result = regmap_write(st->map, st->reg->user_ctrl,
			      INV_MPU6050_BIT_FIFO_EN);
	if (result)
		goto reset_fifo_fail;

	/* setup wait irq flag */
	st->wait_irq = 1;

	/* enable interrupt */
	if (st->chip_config.accl_fifo_enable ||
	    st->chip_config.gyro_fifo_enable ||
	    st->chip_config.temp_fifo_enable) {
		result = regmap_write(st->map, st->reg->int_enable,
				      INV_MPU6050_BIT_DATA_RDY_EN);
		if (result)
			goto reset_fifo_fail;
	}

	/* wait first irq */
	wait_event(st->irq_wq, st->wait_irq == 0);

	/* enable sensor output to FIFO */
	d = 0;
	if (st->chip_config.gyro_fifo_enable)
		d |= INV_MPU6050_BITS_GYRO_OUT;
	if (st->chip_config.accl_fifo_enable)
		d |= INV_MPU6050_BIT_ACCEL_OUT;
	if (st->chip_config.temp_fifo_enable)
		d |= INV_MPU6050_BIT_TEMP_OUT;

	result = regmap_write(st->map, st->reg->fifo_en, d);
	if (result)
		goto reset_fifo_fail;

	return 0;

reset_fifo_fail:
	dev_err(regmap_get_device(st->map), "reset fifo failed %d\n", result);
	result = regmap_write(st->map, st->reg->int_enable,
			      INV_MPU6050_BIT_DATA_RDY_EN);

	return result;
}

/**
 * inv_mpu6050_irq_handler() - Cache a timestamp at each data ready interrupt.
 */
irqreturn_t inv_mpu6050_irq_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	s64 timestamp;

	timestamp = iio_get_time_ns(indio_dev);
	kfifo_in_spinlocked(&st->timestamps, &timestamp, 1,
			    &st->time_stamp_lock);

	return IRQ_WAKE_THREAD;
}

/**
 * inv_mpu6050_create_timestamped_buffer() - create buffer with timestamps
 *
 * Samples are stored in st->rx_buffer_ts
 */
static int inv_mpu6050_create_timestamped_buffer(struct iio_dev *indio_dev,
						 size_t bytes_per_datum,
						 u16 samples)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	s64 timestamp = 0;
	u8 *src;
	u8 *dest;
	size_t ts_padding;
	size_t buff_size;
	size_t i;
	int result;

	/* Compute required buffer size */
	ts_padding = sizeof(s64) - (bytes_per_datum % sizeof(s64));
	buff_size = (bytes_per_datum + ts_padding + sizeof(s64)) * samples;

	/* Resize buffer if required */
	if (buff_size > st->rx_buffer_ts_size) {
		st->rx_buffer_ts = krealloc(st->rx_buffer_ts, buff_size,
					    GFP_KERNEL);
		if (!st->rx_buffer_ts) {
			st->rx_buffer_size = 0;
			return -ENOMEM;
		}

		st->rx_buffer_size = buff_size;
	}

	dest = st->rx_buffer_ts;
	src = st->rx_buffer;

	/* get last sample timestamp and estimate first sample timestamp */
	result = kfifo_out(&st->timestamps, &timestamp, 1);
	if (result == 0) {
		dev_err(&indio_dev->dev, "no timestamp\n");
		timestamp = 0;
	} else {
		timestamp -= st->chip_config.fifo_period_ns* (samples - 1);
	}

	for (i = 0; i < samples; i++) {
		/* Store sample as-is, no padding required */
		memcpy(dest, src, bytes_per_datum);
		src += bytes_per_datum;
		dest += bytes_per_datum;

		/* Store timestamp */
		memcpy(dest + ts_padding, &timestamp, sizeof(s64));
		dest += ts_padding + sizeof(s64);

		/* Compute next sample timestamp */
		timestamp += st->chip_config.fifo_period_ns;
	}

	return 0;
}

/**
 * inv_mpu6050_hw_fifo_read() - Transfer data from hardware FIFO to a buffer.
 *
 * Samples are stored in st->rx_buffer
 */
static int inv_mpu6050_hw_fifo_read(struct iio_dev *indio_dev,
				    u16 fifo_count)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;

	if (st->chip_type == INV_MPU6000)
		return inv_mpu6000_spi_hw_fifo_read(indio_dev, fifo_count);

	/* Grow buffer if required */
	if (st->rx_buffer_size < fifo_count) {
		st->rx_buffer = krealloc(st->rx_buffer, fifo_count, GFP_KERNEL);
		if (!st->rx_buffer) {
			st->rx_buffer_size = 0;
			return -ENOMEM;
		}

		st->rx_buffer_size = fifo_count;
	}

	result = regmap_bulk_read(st->map, st->reg->fifo_r_w, st->rx_buffer,
			fifo_count);
	return (result < 0) ? result : fifo_count;
}

/**
 * inv_mpu6050_read_fifo() - Transfer data from hardware FIFO to KFIFO.
 */
irqreturn_t inv_mpu6050_read_fifo(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	size_t samples, bytes_per_datum;
	int result;
	u16 fifo_count;
	u16 raw_fifo_count;

	mutex_lock(&indio_dev->mlock);

	if (!(st->chip_config.accl_fifo_enable |
	      st->chip_config.gyro_fifo_enable |
	      st->chip_config.temp_fifo_enable))
		goto end_session;

	/* Calculate IMU sample size (depends of enabled channels) */
	bytes_per_datum = 0;
	if (st->chip_config.accl_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.gyro_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_PER_3AXIS_SENSOR;

	if (st->chip_config.temp_fifo_enable)
		bytes_per_datum += INV_MPU6050_BYTES_TEMPERATURE;

	/*
	 * read fifo_count register to know how many bytes inside FIFO
	 * right now
	 */
	if (st->chip_type == INV_MPU6000)
		fifo_count = inv_mpu6000_spi_hw_fifocount_read(indio_dev);
	else {
		result = regmap_bulk_read(st->map, st->reg->fifo_count_h,
				&raw_fifo_count,
				INV_MPU6050_FIFO_COUNT_BYTE);
		if (result) {
			dev_err(&indio_dev->dev, "regmap_bulk_read fail %d\n", result);
			goto end_session;
		}

		fifo_count = be16_to_cpup((__be16 *)(&raw_fifo_count));
	}
	if (fifo_count < bytes_per_datum) {
		dev_err(&indio_dev->dev, "fifo_count %d < bytes_per_datum "
			"%zu\n", fifo_count, bytes_per_datum);
		goto end_session;
	}

	samples = fifo_count / bytes_per_datum;

	/* fifo count can't be odd number, if it is odd, reset fifo*/
	if (fifo_count & 1) {
		dev_err(&indio_dev->dev, "fifo_count can't be odd %d\n",
			fifo_count);
		goto flush_fifo;
	}

	if (fifo_count > INV_MPU6050_FIFO_THRESHOLD) {
		dev_err(&indio_dev->dev, "fifo_count %d > threshold %zu\n", fifo_count,
			(size_t)INV_MPU6050_FIFO_THRESHOLD);
		goto flush_fifo;
	}

	if (samples != st->trigger_rate_divider)
		dev_err(&indio_dev->dev, "samples:%zu", samples);

	/* Timestamp mismatch. */
	if (kfifo_len(&st->timestamps) > samples + INV_MPU6050_TIME_STAMP_TOR) {
		dev_err(&indio_dev->dev, "timestamp mismatch %zu\n",
			samples);
		goto flush_fifo;
	}

	/* Read all data from hw fifo */
	result = inv_mpu6050_hw_fifo_read(indio_dev, fifo_count);
	if (result < 0) {
		dev_err(&indio_dev->dev, "inv_mpu6050_hw_fifo_read %d failed "
			"%d\n", fifo_count, result);
		goto flush_fifo;
	}

	/* Save current temperature */
	if(st->chip_config.temp_fifo_enable) {
		/* When accelero enabled, temp is provided after */
		if (st->chip_config.accl_fifo_enable)
			st->last_temp = (st->rx_buffer[6] << 8) + st->rx_buffer[7];
		else
			st->last_temp = (st->rx_buffer[0] << 8) + st->rx_buffer[1];
	}

	if (indio_dev->scan_timestamp) {
		result = inv_mpu6050_create_timestamped_buffer(indio_dev,
						bytes_per_datum, samples);
		if (result) {
			dev_err(&indio_dev->dev, "inv_mpu6050_create_timestamped_buffer fail %d\n", result);
			goto flush_fifo;
		}

		result = iio_push_to_buffers_n(indio_dev, st->rx_buffer_ts,
						samples);
		if (result) {
			if (!st->push_buffer_failed) {
				dev_err(&indio_dev->dev, "iio_push_to_buffers_n fail %d\n", result);
				st->push_buffer_failed = 1;
			}
			goto flush_fifo;
		} else if (st->push_buffer_failed) {
			dev_info(&indio_dev->dev, "iio_push_to_buffers_n succeed\n");
			st->push_buffer_failed = 0;
		}
	} else {
		result = iio_push_to_buffers_n(indio_dev, st->rx_buffer,
					       samples);
		if (result)
			goto flush_fifo;
	}

end_session:
	mutex_unlock(&indio_dev->mlock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;

flush_fifo:
	/* Flush HW and SW FIFOs. */
	inv_reset_fifo(indio_dev);
	mutex_unlock(&indio_dev->mlock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}
