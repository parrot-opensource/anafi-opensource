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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/iio/iio.h>

#include "parrot_bldc_iio.h"
#include "parrot_bldc_cypress_iio.h"

static const struct iio_chan_spec bldc_cypress_channels[] = {
	/* keep 4 first motors channels at first table place (LUT remap)*/
	PARROT_BLDC_OBS_CHAN(SPEED_MOTOR1, 0, IIO_ANGL_VEL, 24, 24),
	PARROT_BLDC_OBS_CHAN(SPEED_MOTOR2, 1, IIO_ANGL_VEL, 24, 24),
	PARROT_BLDC_OBS_CHAN(SPEED_MOTOR3, 2, IIO_ANGL_VEL, 24, 24),
	PARROT_BLDC_OBS_CHAN(SPEED_MOTOR4, 3, IIO_ANGL_VEL, 24, 24),
	PARROT_BLDC_OBS_CHAN(BATT_VOLTAGE, 0, IIO_VOLTAGE, 16, 16),
	PARROT_BLDC_OBS_CHAN_EXT_NAME(STATUS,       0, IIO_CURRENT, 8, 8),
	PARROT_BLDC_OBS_CHAN_EXT_NAME(ERRNO,        1, IIO_CURRENT, 8, 8),
	PARROT_BLDC_OBS_CHAN_EXT_NAME(FAULT_MOTOR,  2, IIO_CURRENT, 4, 8),
	PARROT_BLDC_OBS_CHAN(TEMP,         0, IIO_TEMP, 8, 8),
	IIO_CHAN_SOFT_TIMESTAMP(PARROT_BLDC_SCAN_TIMESTAMP),
};

static ssize_t bldc_cypress_store_clear_error(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int ret;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_CLEAR_ERROR, 0, NULL);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_led(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	u8 data;
	int ret;

	ret = kstrtou8(buf, 10, &data);
	if (ret)
		return -EINVAL;

	/* bits reserved */
	data &= 0x3;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_LED, 1, &data);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_motors_speed(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int ret, i;
	u8 data[10];
	u8 crc;
	unsigned short m[4];
	unsigned short *pm = (__be16 *)&data[0];

	ret = sscanf(buf, "%hu %hu %hu %hu",
			&m[0],
			&m[1],
			&m[2],
			&m[3]);

	if (ret != 4)
		return -EINVAL;

	for (i = 0; i < 4; i++)
		*pm++ = cpu_to_be16(m[st->pdata.lut[i]]);

	data[8] = 0; /* force enable security to 0 */

	crc = PARROT_BLDC_REG_REF_SPEED;
	for (i = 0; i < 9; i++)
		crc ^= data[i];
	data[9] = crc;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_REF_SPEED, 10, data);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_reboot(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	u8 data = 4;
	int ret;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_LED, 1, &data);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_start_with_dir(struct iio_dev *indio_dev,
		struct bldc_state *st, u8 spin_dir)
{
	int ret, i;
	u8 data;

	/* apply lut on spin direction */
	data = 0;
	for (i = 0; i < 4; i++) {
		if (spin_dir & (1 << st->pdata.lut[i]))
			data |= 1 << i;
	}

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_START_PROP, 1, &data);

	return ret;
}

static ssize_t bldc_cypress_store_start(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int ret;

	/* use spin direction given in platform data */
	ret = bldc_cypress_start_with_dir(indio_dev, st, st->pdata.spin_dir);
	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_start_with_dir(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	u8 spin_dir;
	int ret;

	ret = kstrtou8(buf, 10, &spin_dir);
	if (ret)
		return -EINVAL;

	/* use spin direction given in argument */
	ret = bldc_cypress_start_with_dir(indio_dev, st, spin_dir);
	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_stop(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int ret;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_STOP_PROP, 0, NULL);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_show_info(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int result;
	__u8 ibuf[PARROT_BLDC_GET_INFO_LENGTH];
	unsigned short nb_flights;
	unsigned short previous_time;
	unsigned int total_time;

	result = st->tf->read_multiple_byte(st->dev,
			PARROT_BLDC_REG_INFO,
			PARROT_BLDC_GET_INFO_LENGTH, ibuf, 0);
	if (result) {
		dev_err(dev, "show_info: null state\n");
		return -EINVAL;
	}

	nb_flights    = ibuf[5] << 8 | ibuf[6];
	previous_time = ibuf[7] << 8 | ibuf[8];
	total_time    = ibuf[9] << 24 | ibuf[10] << 16
			| ibuf[11] << 8 | ibuf[12];

	return sprintf(buf, "%d %d %c %d %d %hu %hu %u %d\n",
			ibuf[0],
			ibuf[1],
			ibuf[2],
			ibuf[3],
			ibuf[4],
			nb_flights,
			previous_time,
			total_time,
			ibuf[13]);
}

static ssize_t bldc_cypress_store_clear_infos(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);
	int ret;

	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_CLEAR_INFO, 0, NULL);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_show_spin_dir(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);

	if (!st)
		return -ENOENT;

	return sprintf(buf, "%d\n", st->pdata.spin_dir);
}

static ssize_t bldc_cypress_show_play_sound(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);

	if (!st) {
		dev_err(dev, "show_play_sound: null state\n");
		return -ENOENT;
	}

	return sprintf(buf, "%hhu\n", st->sound);
}

static ssize_t bldc_cypress_store_play_sound(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	uint8_t data[1];
	uint8_t sound;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);

	if (!st) {
		dev_err(dev, "store_play_sound: null state\n");
		return -ENOENT;
	}

	if (count < 1)
		return -EINVAL;

	/* parse buffer argument */
	ret = sscanf(buf, "%hhu", &sound);
	if (ret != 1 || sound > 128)
		return -EINVAL;

	/* save new sound state */
	st->sound = sound;

	/* send sound command and argument over i2c
	 * negative sound means infinite (until stop) */
	data[0] = -st->sound;
	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_TEST_SOUND, sizeof(data), data);

	return ret == 0 ? count : ret;
}

static ssize_t bldc_cypress_store_play_custom_sound(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	uint8_t data[5];
	int8_t amplitude;
	uint16_t half_period_us;
	uint16_t duration_ms;
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct bldc_state *st = iio_priv(indio_dev);

	if (!st) {
		dev_err(dev, "store_play_custom_sound: null state\n");
		return -ENOENT;
	}

	if (count < 1)
		return -EINVAL;

	/* parse buffer arguments */
	ret = sscanf(buf, "%hhi %hu %hu", &amplitude,
					  &half_period_us,
					  &duration_ms);
	if (ret != 3)
		return -EINVAL;

	/* send custom_sound command and arguments over i2c */
	data[0] = amplitude;
	data[1] = (half_period_us >> 8) & 0xFF;
	data[2] = (half_period_us >> 0) & 0xFF;
	data[3] = (duration_ms >> 8) & 0xFF;
	data[4] = (duration_ms >> 0) & 0xFF;
	ret = st->tf->write_multiple_byte(st->dev,
			PARROT_BLDC_REG_TEST_SOUND, sizeof(data), data);

	return ret == 0 ? count : ret;
}

static IIO_DEVICE_ATTR(start,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_start,
		       ATTR_START_MOTORS);

static IIO_DEVICE_ATTR(start_with_dir,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_start_with_dir,
		       ATTR_START_MOTORS_WITH_DIR);

static IIO_DEVICE_ATTR(stop,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_stop,
		       ATTR_STOP_MOTORS);

static IIO_DEVICE_ATTR(led,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_led,
		       ATTR_LED);

static IIO_DEVICE_ATTR(reboot,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_reboot,
		       ATTR_REBOOT);

static IIO_DEVICE_ATTR(clear_error,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_clear_error,
		       ATTR_CLEAR_ERROR);

static IIO_DEVICE_ATTR(motors_speed,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_motors_speed,
		       ATTR_MOTORS_SPEED);

static IIO_DEVICE_ATTR(flight_infos,
		       S_IRUGO,
		       bldc_cypress_show_info,
		       NULL,
		       ATTR_INFO_FT_GET);

static IIO_DEVICE_ATTR(clear_flight_infos,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_clear_infos,
		       ATTR_INFO_CLEAR_FT);

static IIO_DEVICE_ATTR(spin_dir,
		       S_IRUGO,
		       bldc_cypress_show_spin_dir,
		       NULL,
		       ATTR_INFO_SPIN_DIR);

static IIO_DEVICE_ATTR(play_sound,
		       S_IWUSR | S_IRUGO,
		       bldc_cypress_show_play_sound,
		       bldc_cypress_store_play_sound,
		       ATTR_PLAY_SOUND);

static IIO_DEVICE_ATTR(play_custom_sound,
		       S_IWUSR,
		       NULL,
		       bldc_cypress_store_play_custom_sound,
		       ATTR_PLAY_CUSTOM_SOUND);

static struct attribute *inv_attributes[] = {
	&iio_dev_attr_start.dev_attr.attr,
	&iio_dev_attr_start_with_dir.dev_attr.attr,
	&iio_dev_attr_stop.dev_attr.attr,
	&iio_dev_attr_led.dev_attr.attr,
	&iio_dev_attr_reboot.dev_attr.attr,
	&iio_dev_attr_clear_error.dev_attr.attr,
	&iio_dev_attr_motors_speed.dev_attr.attr,
	&iio_dev_attr_flight_infos.dev_attr.attr,
	&iio_dev_attr_clear_flight_infos.dev_attr.attr,
	&iio_dev_attr_spin_dir.dev_attr.attr,
	&iio_dev_attr_play_sound.dev_attr.attr,
	&iio_dev_attr_play_custom_sound.dev_attr.attr,
	NULL,
};

static const struct attribute_group inv_attribute_group = {
	.attrs = inv_attributes
};

static int bldc_update_scan_mode(struct iio_dev *indio_dev,
	const unsigned long *scan_mask)
{
	struct bldc_state *st = iio_priv(indio_dev);

	kfree(st->buffer);

	st->buffer = kmalloc(indio_dev->scan_bytes +
			PARROT_BLDC_OBS_DATA_LENGTH,
			GFP_KERNEL);
	if (st->buffer == NULL)
		return -ENOMEM;

	return 0;
}

static const struct iio_info bldc_cypress_info = {
	.update_scan_mode	= bldc_update_scan_mode,
	.driver_module		= THIS_MODULE,
	.attrs			= &inv_attribute_group,
};

static int bldc_cypress_iio_buffer_new(struct iio_dev *indio_dev)
{
	int err = 0;

	struct bldc_state *st = iio_device_get_drvdata(indio_dev);

	indio_dev->modes = INDIO_BUFFER_TRIGGERED;
	err = iio_triggered_buffer_setup(indio_dev,
					 NULL,
					 bldc_cypress_read_fifo,
					 NULL);
	if (err < 0) {
		dev_err(st->dev, "configure buffer fail %d\n", err);
		goto exit;
	}
exit:
	return err;
}

static void bldc_cypress_iio_buffer_cleanup(struct iio_dev *indio_dev)
{
	iio_triggered_buffer_cleanup(indio_dev);
}

int bldc_cypress_probe(struct iio_dev *indio_dev)
{
	struct bldc_state *st;
	int result, i;

	st = iio_priv(indio_dev);

	/* copy default channels */
	memcpy(st->channels, bldc_cypress_channels, sizeof(st->channels));

	/* update first 4 channels scan_index given motor lut */
	for (i = 0; i < 4; i++)
		st->channels[i].scan_index = st->pdata.lut[i];

	indio_dev->channels = st->channels;
	indio_dev->num_channels = ARRAY_SIZE(st->channels);
	indio_dev->info = &bldc_cypress_info;

	result = bldc_cypress_iio_buffer_new(indio_dev);
	if (result < 0)
		goto out_remove_trigger;

	result = devm_iio_device_register(st->dev, indio_dev);
	if (result) {
		dev_err(st->dev, "IIO register fail %d\n", result);
		goto out_remove_trigger;
	}

	dev_info(st->dev,
		 "PARROT BLDC (%s) registered\n",
		 indio_dev->name);

	return 0;

out_remove_trigger:
	bldc_cypress_iio_buffer_cleanup(indio_dev);
	return result;
}
EXPORT_SYMBOL(bldc_cypress_probe);

void bldc_cypress_remove(struct iio_dev *indio_dev)
{
	struct bldc_state *st = iio_priv(indio_dev);

	devm_iio_device_unregister(st->dev, indio_dev);
	bldc_cypress_iio_buffer_cleanup(indio_dev);
	kfree(st->buffer);
}
EXPORT_SYMBOL(bldc_cypress_remove);

MODULE_AUTHOR("Karl Leplat <karl.leplat@parrot.com>");
MODULE_DESCRIPTION("Parrot BLDC cypress IIO driver");
MODULE_LICENSE("GPL");
