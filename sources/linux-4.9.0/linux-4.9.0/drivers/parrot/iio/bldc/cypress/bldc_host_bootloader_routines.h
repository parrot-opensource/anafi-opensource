/**
 ************************************************
 *@file bldc_host_bootloader_routines.h
 *@brief Servo firmware loading routines.
 *
 *Copyright (C) 2015 Parrot S.A.
 *
 *@author Quentin QUADRAT <quentin.quadrat@parrot.com>
 *@author Karl Leplat <karl.lepat@parrot.com>
 *@date 2015-06-22
 *************************************************
 */

#ifndef BLDC_HOST_BOOTLOADER_CMD_H_
#define BLDC_HOST_BOOTLOADER_CMD_H_

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>

#define C_BTLDR_SUM_CHECKSUM            0x00
/* 16-bit CRC checksum using the CCITT implementation */
#define C_BTLDR_CRC_CHECKSUM            0x01

/* The highest number of flash array for any device */
#define C_BTLDR_MAX_FLASH_ARRAYS        4
/* Maximum number of bytes to allocate for a single row.  */
/* NB: Rows should have a max of 592 chars
 * (2-array_id, 4-row_num, 4-len, 576-data, 2-checksum, 4-newline) */
#define C_BTLDR_MAX_BUFFER_SIZE    768

/* The communications object reported an error */
#define C_BTLDR_ERR_COMM_MASK     0x20
/* The bootloader reported an error */
#define C_BTLDR_ERR_BTLDR_MASK    0x40

/******************************************************************************
 *    HOST ERROR CODES
 ******************************************************************************
 *
 * Different return codes from the bootloader host.  Functions are not
 * limited to these values, but are encuraged to use them when returning
 * standard error values.
 *
 * 0 is successful, all other values indicate a failure.
 *****************************************************************************/
enum {
	C_BTLDR_SUCCESS,
	/* Completed successfully */
	C_BTLDR_ERR_FILE,
	/* File is not accessable */
	C_BTLDR_ERR_LENGTH,
	/* The amount of data available is outside the expected range */
	C_BTLDR_ERR_DATA,
	/* The data is not of the proper form */
	C_BTLDR_ERR_DEVICE,
	/* The expected device does not match the detected device */
	C_BTLDR_ERR_VERSION,
	/* The bootloader version detected is not supported */
	C_BTLDR_ERR_CHECKSUM,
	/* The checksum does not match the expected value */
	C_BTLDR_ERR_ARRAY,
	/* The flash array is not valid */
	C_BTLDR_ERR_ROW,
	/* The flash row is not valid */
	C_BTLDR_ERR_UNK,
	/* An unknown error occured */
	C_BTLDR_ABORT,
	/* The operation was aborted */
	C_BTLDR_ERR_EOF,
	/* The operation was aborted */
	C_BTLDR_FAILURE_FIRMWARE_NOT_FLASHED,
	/* Failure flashing the new firmware
	 * + restored succesfully with backup */
	C_BTLDR_FAILURE_NOT_BLDC_FIRMWARE,
	/* Failure on check BLDC: get_version() */
	C_BTLDR_MAX_ERR,
};

/******************************************************************************
 *    BOOTLOADER STATUS CODES
 ******************************************************************************
 *
 * Different return status codes from the bootloader.
 *
 * 0 is successful, all other values indicate a failure.
 *****************************************************************************/
enum {
	C_CY_BTLDR_STAT_SUCCESS,
	/* Completed successfully */
	C_CY_BTLDR_STAT_ERR_KEY,
	/* The provided key does not match the expected value */
	C_CY_BTLDR_STAT_ERR_VERIFY,
	/* The verification of flash failed */
	C_CY_BTLDR_STAT_ERR_LENGTH,
	/* The amount of data available is outside the expected range */
	C_CY_BTLDR_STAT_ERR_DATA,
	/* The data is not of the proper form */
	C_CY_BTLDR_STAT_ERR_CMD,
	/* The command is not recognized */
	C_CY_BTLDR_STAT_ERR_DEVICE,
	/* The expected device does not match the detected device */
	C_CY_BTLDR_STAT_ERR_VERSION,
	/* The bootloader version detected is not supported */
	C_CY_BTLDR_STAT_ERR_CHECKSUM,
	/* The checksum does not match the expected value */
	C_CY_BTLDR_STAT_ERR_ARRAY,
	/* The flash array is not valid */
	C_CY_BTLDR_STAT_ERR_ROW,
	/* The flash row is not valid */
	C_CY_BTLDR_STAT_ERR_PROTECT,
	/* The flash row is protected and can not be programmed */
	C_CY_BTLDR_STAT_ERR_APP,
	/* The application is not valid and cannot be set as active */
	C_CY_BTLDR_STAT_ERR_ACTIVE,
	/* The application is currently marked as active */
	C_CY_BTLDR_STAT_ERR_UNK2,
	C_CY_BTLDR_STAT_ERR_UNK,
	/* An unknown error occured */
	C_CY_BTLDR_STAT_MAX_ERR,
};

uint8_t bldc_btldr_parse_header(const uint16_t buf_size,
		const uint8_t *const buffer,
		uint32_t *const silicon_id,
		uint32_t *const silicon_rev,
		uint8_t *const chksum_type);

uint8_t bldc_btldr_start_bootload_operation(struct i2c_client *client,
		const uint32_t expsi_id,
		const uint32_t expsi_rev,
		uint32_t *const silicon_id,
		uint32_t *const silicon_rev,
		uint32_t *const boot_ver,
		uint32_t *valid_rows,
		const uint8_t chksum_type);

uint8_t bldc_btldr_parse_row_data(const uint16_t buf_size,
		const uint8_t *const buffer,
		uint8_t *const array_id,
		uint16_t *const row_num,
		uint8_t *const row_data,
		uint16_t *const size,
		uint8_t *const chksum_type);

uint8_t bldc_btldr_end_bootloadoperation(struct i2c_client *client,
		const uint8_t chksum_type);

uint8_t bldc_btldr_program_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t *const buf,
		const uint16_t size,
		const uint8_t chksum_type);

uint8_t bldc_btldr_erase_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t chksum_type);

uint8_t bldc_btldr_verify_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t checksum,
		const uint8_t chksum_type);

uint8_t bldc_btldr_verify_application(struct i2c_client *client,
		const uint8_t chksum_type);

uint8_t bldc_btldr_send_start_bootload_operation(struct i2c_client *client,
		uint32_t *valid_rows,
		const uint8_t chksum_type);

uint8_t bldc_btldr_read_md5(struct i2c_client *client,
		const uint8_t chksum_type, uint8_t *md5, size_t size);

uint8_t bldc_btldr_write_md5(struct i2c_client *client,
		const uint8_t chksum_type, const uint8_t *md5, size_t size);

const char *bootloader_string_error(const uint8_t id);

#endif /* BLDC_HOST_BOOTLOADER_CMD_H_ */
