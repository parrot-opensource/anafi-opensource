/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc_cmd.h
 * @brief Ambarella A9S ADC IIO driver cmd header
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-09
 */

#ifndef __AMBA_A9S_ADC_CMD_H__
#define __AMBA_A9S_ADC_CMD_H__

/**
 * @brief Init ThreadX or local backend
 *
 * @return 0 on success, and an appropriate error value on failure.
 */
int amba_a9s_adc_cmd_init(void *priv);

/**
 * @brief Cleanup ThreadX or local backend
 */
void amba_a9s_adc_cmd_exit(void);

/**
 * @brief Send ADC command to ThreadX or local backend
 *
 * @param data command
 * @param len length of command
 *
 * @return 0 on success and an appropriate error value on failure.
 */
int amba_a9s_adc_cmd(void *data, int len);
#endif
