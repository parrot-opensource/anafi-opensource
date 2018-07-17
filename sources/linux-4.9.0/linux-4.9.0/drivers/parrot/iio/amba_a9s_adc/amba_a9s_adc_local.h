/**
 * Copyright (c) 2017 Parrot SA
 *
 * @file amba_a9s_adc_local.h
 * @brief Ambarella A9S ADC IIO local cmd (Linux stand-alone) header
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2017-02-09
 */
#ifndef __AMBA_A9S_ADC_LOCAL_H__
#define __AMBA_A9S_ADC_LOCAL_H__

struct amba_a9s_adc_local_platform_data {
	u32 period;
	u32 highfreq_channels;
	u32 lowfreq_channels[4];
};
#endif
