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

#include <linux/slab.h>
#include <linux/err.h>
#include <linux/spi/spi.h>
#include "inv_mpu_iio.h"

static int mpu6000_fast_read(struct inv_mpu6050_state *st, const void *buf_tx, void *buf_rx, size_t len)
{
	struct device *dev = regmap_get_device(st->map);
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer     t = {
		.tx_buf         = buf_tx,
		.rx_buf         = buf_rx,
		.len            = len,
		/* up to 20 Mhz */
		.speed_hz = 5000000,
	};

	return spi_sync_transfer(spi, &t, 1);
}

/**
 * inv_mpu6050_hw_fifo_read() - Transfer data from hardware FIFO to a buffer.
 *
 * Samples are stored in st->rx_buffer
 */
int inv_mpu6000_spi_hw_fifo_read(struct iio_dev *indio_dev,
				    u16 fifo_count)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;

	/* Grow buffer if required */
	if (st->rx_buffer_size < fifo_count) {
		st->rx_buffer_raw = krealloc(st->rx_buffer_raw, fifo_count+1, GFP_KERNEL);
		if (!st->rx_buffer_raw) {
			st->rx_buffer_size = 0;
			return -ENOMEM;
		}
		st->tx_buffer = krealloc(st->tx_buffer, fifo_count+1, GFP_KERNEL);


		st->rx_buffer = st->rx_buffer_raw + 1;
		st->rx_buffer_size = fifo_count;
	}

	st->tx_buffer[0] = st->reg->fifo_r_w | 0x80;
	result = mpu6000_fast_read(st, st->tx_buffer, st->rx_buffer_raw,
			fifo_count + 1);
	return (result < 0) ? result : fifo_count;
}

int inv_mpu6000_spi_hw_fifocount_read(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int result;
	int fifo_count;
	/*
	 * read fifo_count register to know how many bytes inside FIFO
	 * right now
	 */
	st->raw_cmd[0] = st->reg->fifo_count_h | 0x80;
	result = mpu6000_fast_read(st, st->raw_cmd, st->raw_cmd+INV_MPU6050_FIFO_COUNT_BYTE+1,
			INV_MPU6050_FIFO_COUNT_BYTE+1);
	if (result) {
		dev_err(&indio_dev->dev, "regmap_bulk_read fail %d\n", result);
		return -1;
	}

	fifo_count = be16_to_cpup((__be16 *)(st->raw_cmd+4));
	return fifo_count;
}

