/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc.h
 * @brief Ambarella A9S ADC IIO driver header
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-04
 */

#ifndef __AMBA_A9S_ADC_H__
#define __AMBA_A9S_ADC_H__

#include <linux/kernel.h>
#include <plat/adc.h>   /* ADC_NUM_CHANNELS */

#if defined(CONFIG_IIO_AMBA_A9S_ADC_LOCAL)
#include "amba_a9s_adc_cmd_interface.h"
#include "amba_a9s_adc_local.h"
#endif

/**
 * struct amba_a9s_adc_state - device instance specific state.
 * @adc_val:	cache for adc values
 * @lock:			lock to ensure state is consistent
 * @wq_data_available: wait queue for telling consumer data is ready
 * @read_raw_done:     used for read_raw new data availability
 */
struct amba_a9s_adc_state {
	int adc_val[ADC_NUM_CHANNELS];
	struct mutex lock;
	wait_queue_head_t wq_data_available;
	bool read_raw_done;
	struct iio_dev *indio_dev;

/* When *not* delegating ADC work to ThreadX, we need more state variables */
#if defined(CONFIG_IIO_AMBA_A9S_ADC_LOCAL)
  struct platform_device *pdev;
  void __iomem  *regbase;
  struct regmap *rctregbase;
  struct amba_a9s_adc_local_platform_data pdata;
  s32 irq;
  struct amba_parrot_adc_capture_sample_s *buffer;
  bool capture_enable;
  u32 idx;
  u32 count;
  u32 nb_samples;
#endif
};

/**
 * enum amba_a9s_adc_scan_elements - scan index enum
 * @voltage0:		the single ended voltage channel
 *
 * Enum provides convenient numbering for the scan index.
 */
enum amba_a9s_adc_scan_elements {
	voltage0,
	voltage1,
	voltage2,
	voltage3,
	voltage4,
	voltage5,
	voltage6,
	voltage7,
	voltage8,
	voltage9,
	voltage10,
	voltage11,
};

/**
 * @brief Macro helper for channel declaration in amba_a9s_adc_channels[]
 *
 * All channel share the same properties (IIO_VOLTAGE, 12 bits sampling, etc)
 *
 * @param _num:  channel number
 *
 */
#define AMBA_ADC_CHANNEL(_num) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.channel = _num, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.scan_index = voltage##_num, \
	.scan_type = { \
		.sign = 'u', \
		.realbits = 12, \
		.storagebits = 32, \
		.shift = 0, \
	} \
}
#endif
