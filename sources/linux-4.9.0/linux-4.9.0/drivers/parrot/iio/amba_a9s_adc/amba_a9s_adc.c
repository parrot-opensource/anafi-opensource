/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc.c
 * @brief Ambarella A9S ADC IIO driver
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-04
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>        /* wait_event_interruptible_timeout() */
#include <linux/wait.h>         /* wait_event_interruptible_timeout() */

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/events.h>
#include <linux/iio/buffer.h>
#include "amba_a9s_adc.h"
#include "amba_a9s_adc_buffer.h"
#include "amba_a9s_adc_cmd.h"
#include "amba_a9s_adc_cmd_interface.h"

/* Pointer array used to fake bus elements */
static struct iio_dev **amba_a9s_adc_devs;

/* Fake a name for the part number, usually obtained from the id table */
static const char *amba_a9s_adc_part_number = "amba_a9s_adc";

/*
 * amba_a9s_adc_channels - Description of available channels
 *
 * This array of structures tells the IIO core about what the device
 * actually provides for a given channel.
 */
static const struct iio_chan_spec amba_a9s_adc_channels[] = {
	AMBA_ADC_CHANNEL(0),
	AMBA_ADC_CHANNEL(1),
	AMBA_ADC_CHANNEL(2),
	AMBA_ADC_CHANNEL(3),
};

/**
 * amba_a9s_adc_read_raw() - data read function.
 * @indio_dev:	the struct iio_dev associated with this device instance
 * @chan:	the channel whose data is to be read
 * @val:	first element of returned value (typically INT)
 * @val2:	second element of returned value (typically MICRO)
 * @mask:	what we actually want to read as per the info_mask_*
 *		in iio_chan_spec.
 */
static int amba_a9s_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val,
			      int *val2,
			      long mask)
{
	struct amba_a9s_adc_state *st = iio_priv(indio_dev);
	int ret = -EINVAL;
	struct amba_parrot_adc_message_s *msg;
	ssize_t length  = 0;
	enum amba_parrot_adc_channel_e channel;

	mutex_lock(&st->lock);
	switch (mask) {
	case IIO_CHAN_INFO_RAW: /* magic value - channel value read */
		switch (chan->type) {
		case IIO_VOLTAGE:
			length = sizeof(struct amba_parrot_adc_message_s) +
			  sizeof(enum amba_parrot_adc_channel_e);
			msg = kcalloc(1, length, GFP_KERNEL);
			if (msg == NULL) {
				ret = -ENOMEM;
				goto end;
			}
			msg->cmd = ADC_GET_DATA;
			msg->length = sizeof(enum amba_parrot_adc_channel_e);
			channel = chan->channel;
			memcpy(msg->buffer, &channel, sizeof(enum amba_parrot_adc_channel_e));
			amba_a9s_adc_cmd((void *)msg, length);
			kfree(msg);
			ret = wait_event_interruptible_timeout(st->wq_data_available,
			                                       st->read_raw_done,
			                                       msecs_to_jiffies(500));
			if (ret == 0)
				ret = -ETIMEDOUT;
			if (ret < 0)
				goto end;

			*val = st->adc_val[chan->channel];
			ret = IIO_VAL_INT;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
end:
	st->read_raw_done = false;
	mutex_unlock(&st->lock);
	return ret;
}

/*
 * Device type specific information.
 */
static const struct iio_info amba_a9s_adc_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &amba_a9s_adc_read_raw,
};

/**
 * amba_a9s_adc_init_device() - device instance specific init
 * @indio_dev: the iio device structure
 *
 * Most drivers have one of these to set up default values,
 * reset the device to known state etc.
 */
static int amba_a9s_adc_init_device(struct iio_dev *indio_dev)
{
	struct amba_a9s_adc_state *st = iio_priv(indio_dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(st->adc_val); i++)
		st->adc_val[i] = 0;

	return 0;
}

/**
 * amba_a9s_adc_probe() - device instance probe
 * @index: an id number for this instance.
 *
 * Arguments are bus type specific.
 * I2C: amba_a9s_adc_probe(struct i2c_client *client,
 *                      const struct i2c_device_id *id)
 * SPI: amba_a9s_adc_probe(struct spi_device *spi)
 */
static int amba_a9s_adc_probe(int index)
{
	int ret;
	struct iio_dev *indio_dev;
	struct amba_a9s_adc_state *st;

	/*
	 * Allocate an IIO device.
	 *
	 * This structure contains all generic state
	 * information about the device instance.
	 * It also has a region (accessed by iio_priv()
	 * for chip specific state information.
	 */
	indio_dev = iio_device_alloc(sizeof(*st));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	st = iio_priv(indio_dev);
	mutex_init(&st->lock);
	init_waitqueue_head(&st->wq_data_available);
	st->read_raw_done = false;
	st->indio_dev = indio_dev;

	/* Init communication with ADC or ThreadX task */
	ret = amba_a9s_adc_cmd_init(st);
	if (ret < 0)
		goto error_ret;

	amba_a9s_adc_init_device(indio_dev);
	/*
	 * With hardware: Set the parent device.
	 * indio_dev->dev.parent = &spi->dev;
	 * indio_dev->dev.parent = &client->dev;
	 */

	 /*
	 * Make the iio_dev struct available to remove function.
	 * Bus equivalents
	 * i2c_set_clientdata(client, indio_dev);
	 * spi_set_drvdata(spi, indio_dev);
	 */
	amba_a9s_adc_devs[index] = indio_dev;


	/*
	 * Set the device name.
	 *
	 * This is typically a part number and obtained from the module
	 * id table.
	 * e.g. for i2c and spi:
	 *    indio_dev->name = id->name;
	 *    indio_dev->name = spi_get_device_id(spi)->name;
	 */
	indio_dev->name = amba_a9s_adc_part_number;

	/* Provide description of available channels */
	indio_dev->channels = amba_a9s_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(amba_a9s_adc_channels);

	/*
	 * Provide device type specific interface functions and
	 * constant data.
	 */
	indio_dev->info = &amba_a9s_adc_info;

	/* Specify that device provides sysfs type interfaces */
	indio_dev->modes = INDIO_DIRECT_MODE;

	ret = amba_a9s_adc_buffer_configure(indio_dev);
	if (ret < 0)
		goto error_free_device;

	ret = iio_device_register(indio_dev);
	if (ret < 0)
		goto error_unconfigure_buffer;
	return 0;

error_unconfigure_buffer:
	amba_a9s_adc_buffer_unconfigure(indio_dev);

error_free_device:
	iio_device_free(indio_dev);
error_ret:
	return ret;
}

/**
 * amba_a9s_adc_remove() - device instance removal function
 * @index: device index.
 *
 * Parameters follow those of amba_a9s_adc_probe for buses.
 */
static int amba_a9s_adc_remove(int index)
{
	/*
	 * Get a pointer to the device instance iio_dev structure
	 * from the bus subsystem. E.g.
	 * struct iio_dev *indio_dev = i2c_get_clientdata(client);
	 * struct iio_dev *indio_dev = spi_get_drvdata(spi);
	 */
	struct iio_dev *indio_dev = amba_a9s_adc_devs[index];


	/* Cleanup communication bus with ADC or Threadx task */
	amba_a9s_adc_cmd_exit();

	/* Buffered capture related cleanup */
	amba_a9s_adc_buffer_unconfigure(indio_dev);

	/* Unregister the device */
	iio_device_unregister(indio_dev);

	/* Device specific code to power down etc */

	/* Free all structures */
	iio_device_free(indio_dev);

	return 0;
}

/**
 * amba_a9s_adc_init() -  device driver registration
 *
 * Varies depending on bus type of the device. As there is no device
 * here, call probe directly. For information on device registration
 * i2c:
 * Documentation/i2c/writing-clients
 * spi:
 * Documentation/spi/spi-summary
 */
static __init int amba_a9s_adc_init(void)
{
	int ret;

	/* Fake a bus */
	amba_a9s_adc_devs = kcalloc(1, sizeof(*amba_a9s_adc_devs),
				 GFP_KERNEL);
	ret = amba_a9s_adc_probe(0);
		if (ret < 0)
			return ret;
	return 0;
}
module_init(amba_a9s_adc_init);

/**
 * amba_a9s_adc_exit() - device driver removal
 *
 * Varies depending on bus type of the device.
 * As there is no device here, call remove directly.
 */
static __exit void amba_a9s_adc_exit(void)
{
	amba_a9s_adc_remove(0);
	kfree(amba_a9s_adc_devs);
}
module_exit(amba_a9s_adc_exit);

MODULE_AUTHOR("Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com");
MODULE_DESCRIPTION("Ambarella A9S ADC IIO driver");
MODULE_LICENSE("GPL v2");
