/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc.c
 * @brief Ambarella A9S ADC IIO driver buffer backend (ThreadX-dependent)
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-27
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitmap.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/kfifo_buf.h>

#include "amba_a9s_adc.h"
#include "amba_a9s_adc_buffer.h"
#include "amba_a9s_adc_cmd.h"
#include "amba_a9s_adc_cmd_interface.h"


int amba_a9s_adc_buffer_postenable(struct iio_dev *indio_dev)
{
	struct amba_parrot_adc_message_s *msg;
	ssize_t length  = 0; /* Length of the whole cmd */
	u32 active_channels, nb_samples = 0;

	/* Sanity */
	if (indio_dev == NULL
			|| indio_dev->buffer == NULL
			|| indio_dev->active_scan_mask == NULL)
		return -EINVAL;

	/* Allocate cmd message */
	length = sizeof(struct amba_parrot_adc_message_s)
		+ sizeof(active_channels)
		+ sizeof(nb_samples);
	msg = kcalloc(1, length, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	/* One-hot encoding channels */
	active_channels = *indio_dev->active_scan_mask;

	/* Limit amount of samples captured */
	if (indio_dev->buffer->length <= AMBA_A9S_ADC_BUFFER_MAX_SAMPLES)
		nb_samples = indio_dev->buffer->length;
	else
		nb_samples = AMBA_A9S_ADC_BUFFER_MAX_SAMPLES;

	/* Fill cmd */
	msg->cmd = ADC_ENABLE_CAPTURE;
	msg->length = sizeof(active_channels) + sizeof(nb_samples); /* Payload length */
	memcpy(msg->buffer, &active_channels, sizeof(active_channels));
	memcpy(msg->buffer+sizeof(active_channels), &nb_samples, sizeof(nb_samples));

	/* Trigger the capture on ThreadX side or locally */
	amba_a9s_adc_cmd((void *)msg, length);
	kfree(msg);

	return 0;
}

int amba_a9s_adc_buffer_predisable(struct iio_dev *indio_dev)
{
	struct amba_parrot_adc_message_s *msg;
	ssize_t length  = 0; /* Length of the whole cmd */

	/* Allocate cmd message */
	length = sizeof(struct amba_parrot_adc_message_s);
	msg = kcalloc(1, length, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	/* Fill cmd */
	msg->cmd = ADC_DISABLE_CAPTURE;
	msg->length = 0;	/* No args */

	/* Stop the capture on ThreadX side or locally */
	amba_a9s_adc_cmd((void *)msg, length);
	kfree(msg);

	return 0;
}

static const struct iio_buffer_setup_ops amba_a9s_adc_buffer_setup_ops = {
	.postenable = &amba_a9s_adc_buffer_postenable,
	.predisable = &amba_a9s_adc_buffer_predisable,
};

int amba_a9s_adc_buffer_configure(struct iio_dev *indio_dev)
{
	int ret;
	struct iio_buffer *buffer;

	/* Allocate a buffer to use - here a kfifo */
	buffer = iio_kfifo_allocate();
	if (!buffer) {
		ret = -ENOMEM;
		return ret;
	}

	iio_device_attach_buffer(indio_dev, buffer);

	/*
	 * Tell the core what device type specific functions should
	 * be run on either side of buffer capture enable / disable.
	 */
	indio_dev->setup_ops = &amba_a9s_adc_buffer_setup_ops;

	/* This mode allows having non triggered buffer capture, that's
	 * what libultrasound wants (= no need to declare and register
	 * a sysfs trigger) */
	indio_dev->modes |= INDIO_BUFFER_SOFTWARE;

	return 0;
}

void amba_a9s_adc_buffer_unconfigure(struct iio_dev *indio_dev)
{
	iio_kfifo_free(indio_dev->buffer);
}
