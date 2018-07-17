/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc_buffer.h
 * @brief Ambarella A9S ADC IIO driver support backend header
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-26
 */

#ifndef __AMBA_A9S_ADC_BUFFER_H__
#define __AMBA_A9S_ADC_BUFFER_H__

#include <linux/iio/iio.h>        /* struct iio_dev */

int amba_a9s_adc_buffer_configure(struct iio_dev *indio_dev);
void amba_a9s_adc_buffer_unconfigure(struct iio_dev *indio_dev);
#endif
