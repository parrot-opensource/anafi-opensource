/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc_cmd_interface.h
 * @brief Ambarella A9S ADC IIO driver cmd interface header
 * This file contains shared data structures with ThreadX code
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-09
 */

#ifndef __AMBA_A9S_ADC_CMD_INTERFACE_H__
#define __AMBA_A9S_ADC_CMD_INTERFACE_H__

#define ADC_RPMSG_CHANNEL "amba_a9s_adc"
#define AMBA_A9S_ADC_BUFFER_MAX_SAMPLES 8192

#define ADC_COMMANDS(_) \
_(ADC_START) \
_(ADC_STOP) \
_(ADC_GET_DATA) \
_(ADC_PUSH_DATA) \
_(ADC_ENABLE_CAPTURE) \
_(ADC_DISABLE_CAPTURE) \
_(ADC_DATA_AVAILABLE) \
_(ADC_DATA_ACK) \
_(ADC_COMMANDS_NUM) \
_(ADC_FORCE_ENUM_SIZE = 0xffffffff)

/**
 * @brief Command ID put in the first byte of RPMSG exchanged
 * with ThreadX
 */
enum amba_parrot_adc_cmd_e {
#define AS_ENUM(c) c,
	ADC_COMMANDS(AS_ENUM)
#undef AS_ENUM
};

/**
 * @brief Variable-sized structure of RPMSG exchanged with ThreadX
 */
struct amba_parrot_adc_message_s {
	enum amba_parrot_adc_cmd_e cmd;
	u32 length;
	u8 buffer[0];
};

struct amba_parrot_adc_capture_sample_s {
  u32 channel;
};

/**
 * @brief Channel ID enum
 */
enum amba_parrot_adc_channel_e {
	AMBA_ADC_CHANNEL0 = 0,              /* ADC Channel-0 */
	AMBA_ADC_CHANNEL1,                  /* ADC Channel-1 */
	AMBA_ADC_CHANNEL2,                  /* ADC Channel-2 */
	AMBA_ADC_CHANNEL3,                  /* ADC Channel-3 */

	AMBA_NUM_ADC_CHANNEL,               /* Number of ADC Channels */
	AMBA_FORCE_ADC_CHANNEL_ENUM_SIZE = 0xffffffff
};
#endif
