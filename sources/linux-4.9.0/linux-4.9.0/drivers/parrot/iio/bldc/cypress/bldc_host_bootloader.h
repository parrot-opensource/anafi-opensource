/*
 ** bldc_host_bootloader.h for Mykonos3 BLDC Host bootloader in /home/qquadrat
 **
 ** Made by Quentin QUADRAT
 ** Mail   <quentin.quadrat@parrot.com>
 **
 ** Started on  Mon Dec 31 10:27:41 2012 Quentin QUADRAT
 */

#ifndef __BLDC_HOST_BOOTLOADER_H__
#define __BLDC_HOST_BOOTLOADER_H__

#include <crypto/md5.h>

#include "bldc_host_bootloader_routines.h"

#define MAX_CYACD_SIZE (1<<20)

/**< Context of the bootloader */
struct host_bootloader {
	const char *firmware;
	uint8_t cyacd_md5_digest[MD5_DIGEST_SIZE];
	uint8_t bldc_md5_digest[MD5_DIGEST_SIZE];
	uint8_t *ptrpos;
	uint8_t line[C_BTLDR_MAX_BUFFER_SIZE];
	uint16_t line_len;
	uint8_t chksum_type;
	uint32_t cyacd_silicon_id;
	uint32_t cyacd_silicon_rev;
	uint32_t bldc_silicon_id;
	uint32_t bldc_silicon_rev;
	uint32_t bldc_bootloader_ver;
	uint32_t valid_rows[C_BTLDR_MAX_FLASH_ARRAYS];
	int err;
	bool row_flashed;
};

int bldc_open_cyacd_file(struct host_bootloader *const bootloader,
			uint8_t *content, int cyacd_size);

int bldc_program(struct i2c_client *client,
			struct host_bootloader *const bootloader,
			uint8_t *fw_data, int fw_len);

int bldc_verify_crc_appli(struct i2c_client *client,
				struct host_bootloader *const bootloader);

void bldc_end_bootloader(struct i2c_client *client,
			struct host_bootloader *const bootloader);

int bldc_read_md5(struct i2c_client *client,
		  struct host_bootloader *const bootloader,
		  uint8_t *md5, size_t size);

int bldc_write_md5(struct i2c_client *client,
		  struct host_bootloader *const bootloader,
		  const uint8_t *md5, size_t size);

#endif /* __BLDC_HOST_BOOTLOADER_H__ */
