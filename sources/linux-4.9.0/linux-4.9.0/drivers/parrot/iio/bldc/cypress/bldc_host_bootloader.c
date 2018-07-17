/**
 ************************************************
 * @file bldc_host_bootloader.c
 * @brief BLDC cypress IIO driver
 *
 * Copyright (C) 2015 Parrot S.A.
 *
 * @author Karl Leplat <karl.leplat@parrot.com>
 * @date 2015-06-22
 *************************************************
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <crypto/hash.h>
#include <linux/scatterlist.h>

#include "bldc_host_bootloader.h"

#define C_NB_COMM_RETRIES (4)

/* Milliseconds of pause after a failed attempt before retry*/
#define C_BTLDR_MS_DELAY_RETRY		(10U)

static const char *c_bootloader_host_error[C_BTLDR_MAX_ERR+1] = {
	"Completed successfully",
	"file access failure with the firmware file",
	"The number of data bytes received is not in the expected range",
	"The motor firmware contains data not of the proper form",
	"Try to communicate with a device which is not the BLDC card",
	"The bootloader version detected is not supported (< v3.30)",
	"The checksum does not match the expected value",
	"The flash array is not valid",
	"The flash row is not valid",
	"An unknown error occured",
	"The operation was aborted",
	"The end of the firmware file was reached while not expected",
	"The new motor firmware could not be flashed, backup firmware restored",
	"The firmware is not a BLDC firmware",
	"An unhandled error occured",
};

static const char *c_bootloader_comm_error[1+1] = {
	"Error detected with communication link",
	"An unhandled error occured",
};

static const char *c_bootloader_bldc_error[C_CY_BTLDR_STAT_MAX_ERR+1] = {
	"Completed successfully received and executed",
	"The provided key does not match the expected value",
	"The verification of flash failed",
	"The amount of data available is outside the expected range",
	"The data is not of the proper form",
	"The command is not recognized",
	"The expected device does not match the detected device",
	"The bootloader version detected is not supported",
	"The checksum does not match the expected value",
	"The flash array is not valid",
	"The flash row is not valid",
	"The flash row is protected and can not be programmed",
	"The application is not valid and cannot be set as active",
	"The application is currently marked as active",
	"An unknown error occured",
	"An unknown error occured",
	"An unhandled error occured",
};

static const char *pr_error(const uint8_t error)
{
	uint8_t id;

	/* Error comming from BLDC bootloader (internal error) */
	if (error & C_BTLDR_ERR_BTLDR_MASK) {
		id = error & (~C_BTLDR_ERR_BTLDR_MASK);
		if (id > C_CY_BTLDR_STAT_MAX_ERR)
			id = C_CY_BTLDR_STAT_MAX_ERR;
		return c_bootloader_bldc_error[id];
	}

	/* Error comming from BLDC bootloader (communication) */
	else if (error & C_BTLDR_ERR_COMM_MASK) {
		id = error & (~C_BTLDR_ERR_COMM_MASK);
		if (id > 1)
			id = 1;
		return c_bootloader_comm_error[id];
	}

	/* Error comming from Host bootloader */
	else {
		id = error;
		if (id > C_BTLDR_MAX_ERR)
			id = C_BTLDR_MAX_ERR;
		return c_bootloader_host_error[id];
	}
}

static uint8_t read_line(uint8_t *fw_data,
		int fw_len,
		uint8_t **ptrpos,
		uint16_t *const size,
		uint8_t *const buffer)
{
	uint8_t err = C_BTLDR_SUCCESS;
	uint16_t len = 0;
	char *end;

	end = strnstr((char *) *ptrpos, "\r\n", fw_len - (*ptrpos - fw_data));
	if (end != NULL) {
		len = end - (char *)*ptrpos;
		memcpy(buffer, *ptrpos, len);
		buffer[len] = '\0';
		*ptrpos = (uint8_t *)(end + 2);
	} else
		return C_BTLDR_ERR_EOF;

	*size = len;

	return err;
}

static uint8_t read_header_line(uint8_t *fw_data, int fw_len,
				struct host_bootloader *const bootloader)
{
	uint8_t err;

	/* Read the header line */
	err = read_line(fw_data,
			fw_len,
			&bootloader->ptrpos,
			&(bootloader->line_len),
			bootloader->line);

	if (C_BTLDR_SUCCESS == err) {
		/* Parse the header line and extract informations in the aim to
		 * start the bootload process. */
		err = bldc_btldr_parse_header(bootloader->line_len,
				bootloader->line,
				&(bootloader->cyacd_silicon_id),
				&(bootloader->cyacd_silicon_rev),
				&(bootloader->chksum_type));
	} else if (C_BTLDR_ERR_EOF == err)
		/* End of file reached at the first line
		 * of the cyacd file => error */
		err = C_BTLDR_ERR_LENGTH;

	return err;
}

static uint8_t start_process(uint8_t *fw_data, int fw_len,
				struct i2c_client *client,
				struct host_bootloader *const bootloader)
{
	uint8_t nb_retries = C_NB_COMM_RETRIES;
	uint8_t err = C_BTLDR_SUCCESS;

	/* Extract informations on the header
	 * (Scilicon versions, checksum type) */
	err = read_header_line(fw_data, fw_len, bootloader);

	/* Error in header: directly abort the process.
	 * Else quit directly the bootloader */
	if (C_BTLDR_SUCCESS == err) {
		do {
			/* Start bootloader and verify
			 * bootloader/Scilicon versions with
			 * values returend by the BLDC bootloader. */
			err = bldc_btldr_start_bootload_operation(client,
					bootloader->cyacd_silicon_id,
					bootloader->cyacd_silicon_rev,
					&(bootloader->bldc_silicon_id),
					&(bootloader->bldc_silicon_rev),
					&(bootloader->bldc_bootloader_ver),
					bootloader->valid_rows,
					bootloader->chksum_type);
			/* do not retry if remote device silicon revision
			 * does not match firmware silicon revision */
			if (err == C_BTLDR_ERR_DEVICE) {
				dev_err(&client->dev,
					"incompatibility between bldc and fw: "
					"bldc silicon id=%x, "
					"cyacd silicon id=%x\n",
					bootloader->bldc_silicon_id,
					bootloader->cyacd_silicon_id);
				return err;
			}

			if (C_BTLDR_SUCCESS != err) {
				if (nb_retries > 0)
					nb_retries--;
				msleep(C_BTLDR_MS_DELAY_RETRY);
			}
		} while ((C_BTLDR_SUCCESS != err) && (0 != nb_retries));
	} else
		dev_err(&client->dev, "%s\n",
				pr_error(err));

	return err;
}

int bldc_program(struct i2c_client *client,
			struct host_bootloader *const bootloader,
			uint8_t *fw_data, int fw_len)
{
	uint8_t nb_retries = C_NB_COMM_RETRIES;
	uint8_t err;
	uint8_t array_id = 0;
	uint16_t row_num;
	uint16_t row_size;
	uint8_t checksum, checksum2;
	uint8_t row_data[C_BTLDR_MAX_BUFFER_SIZE];

	bootloader->row_flashed = false;

	/* Start bootloading: read the cyacd file header, send the
	 * 'start bootloader' command and get infos returned by the
	 * bootloader. */
	err = start_process(fw_data, fw_len, client, bootloader);

	/* Program/Erase/Verify Row */
	if (C_BTLDR_SUCCESS == err) {
		/* Parse all lines from the cyacd file. */
		do {
			err = read_line(fw_data, fw_len , &bootloader->ptrpos,
					&(bootloader->line_len),
						bootloader->line);
			/* Ajouter TU sur Row 127 + EOF */
			if (C_BTLDR_SUCCESS == err) {
				err = bldc_btldr_parse_row_data(
						bootloader->line_len,
						bootloader->line,
						&array_id, &row_num, row_data,
						&row_size, &checksum);
			}

			/* Perform an action following:
			 * Program/Erase/Verify Row */
			if (C_BTLDR_SUCCESS == err) {
				nb_retries = C_NB_COMM_RETRIES;
				do {

					/* Programm row */
					err = bldc_btldr_program_row(client,
						array_id, row_num,
						bootloader->valid_rows,
						row_data, row_size,
						bootloader->chksum_type);
					if (C_BTLDR_SUCCESS == err) {
						/* Set a flag to force
						 * backup firmware restoration
						 * in case of failure when
						 * falshing the new firmware */
						bootloader->row_flashed = true;

						/* Verify row */
						checksum2 = (uint8_t) (checksum
							+ array_id
							+ row_num
							+ (row_num >> 8)
							+ row_size
							+ (row_size >> 8));
						err = bldc_btldr_verify_row(
							client,
							array_id,
							row_num,
							bootloader->valid_rows,
							checksum2,
							bootloader->
								chksum_type);

						if (err != C_BTLDR_SUCCESS)
							dev_err(&client->dev,
								"%s\n",
								pr_error(err));
					} else {
						dev_err(&client->dev, "%s\n",
								pr_error(err));
					}

					/* Try to send the same message
					 * many times before considering
					 * as critical failure */
					if (C_BTLDR_SUCCESS != err) {
						dev_err(&client->dev,
							"I2C failed ret(%d)\n",
							err);

						if (nb_retries > 0)
							nb_retries--;

						msleep(C_BTLDR_MS_DELAY_RETRY);
					}
				} while ((C_BTLDR_SUCCESS != err)
					 && (0 != nb_retries));
			}
		} while (C_BTLDR_SUCCESS == err);

		if (C_BTLDR_ERR_EOF == err)
			err = C_BTLDR_SUCCESS;
	}

	/* Verify that the entire application is valid */
	if (C_BTLDR_SUCCESS == err) {
		nb_retries = C_NB_COMM_RETRIES;
		do {
			/* Support for full flash verify added in v2.20
			 * of cy_boot */
			err = bldc_btldr_verify_application(client,
						bootloader->chksum_type);
			if (err == C_BTLDR_ERR_COMM_MASK) {
				if (nb_retries > 0)
					nb_retries--;
				msleep(C_BTLDR_MS_DELAY_RETRY);
			}
		} while ((err == C_BTLDR_ERR_COMM_MASK) && (0 != nb_retries));
	}

	return err;
}
EXPORT_SYMBOL(bldc_program);

int bldc_read_md5(struct i2c_client *client,
		  struct host_bootloader *const bootloader,
		  uint8_t *md5, size_t size)
{
	return bldc_btldr_read_md5(client, bootloader->chksum_type, md5, size);
}

EXPORT_SYMBOL(bldc_read_md5);


int bldc_write_md5(struct i2c_client *client,
		  struct host_bootloader *const bootloader,
		  const uint8_t *md5, size_t size)
{
	return bldc_btldr_write_md5(client, bootloader->chksum_type, md5,
			size);
}

EXPORT_SYMBOL(bldc_write_md5);

int bldc_verify_crc_appli(struct i2c_client *client,
				struct host_bootloader *const bootloader)
{
	uint8_t nb_retries = C_NB_COMM_RETRIES;
	int err;

	bootloader->row_flashed = false;

	bootloader->chksum_type = C_BTLDR_SUM_CHECKSUM;
	err = bldc_btldr_send_start_bootload_operation(client,
			bootloader->valid_rows,
			bootloader->chksum_type);

	if (C_BTLDR_SUCCESS == err) {

		/* read current bldc fw md5sum */
		err =  bldc_read_md5(client, bootloader,
			bootloader->bldc_md5_digest,
			sizeof(bootloader->bldc_md5_digest));
		if (err != 0)
			dev_warn(&client->dev,
				"can't read bldc fw md5: err=%d", err);

		nb_retries = C_NB_COMM_RETRIES;
		do {
			/* Support for full flash verify
			 * added in v2.20 of cy_boot */
			err = bldc_btldr_verify_application(client,
					bootloader->chksum_type);
			if (err == C_BTLDR_ERR_COMM_MASK) {
				if (nb_retries > 0)
					nb_retries--;

				msleep(C_BTLDR_MS_DELAY_RETRY);
			}
		} while ((err == C_BTLDR_ERR_COMM_MASK) && (0 != nb_retries));
	}

	return err;
}
EXPORT_SYMBOL(bldc_verify_crc_appli);

void bldc_end_bootloader(struct i2c_client *client,
			struct host_bootloader *const bootloader)
{
	uint8_t nb_retries = C_NB_COMM_RETRIES;
	int err;
	do {
		/* Support for full flash verify added in v2.20 of cy_boot */
		err = bldc_btldr_end_bootloadoperation(client,
				bootloader->chksum_type);
		if (err == C_BTLDR_ERR_COMM_MASK) {
			if (nb_retries > 0)
				nb_retries--;
			msleep(C_BTLDR_MS_DELAY_RETRY);
		}
	} while ((err == C_BTLDR_ERR_COMM_MASK) && (0 != nb_retries));
}
EXPORT_SYMBOL(bldc_end_bootloader);

int bldc_open_cyacd_file(struct host_bootloader *const bootloader,
		uint8_t *content, int cyacd_size)
{
	struct crypto_shash *tfm;

	memset(bootloader->cyacd_md5_digest, 0, sizeof(bootloader->cyacd_md5_digest));

	bootloader->ptrpos = content;
	if (!bootloader->ptrpos) {
		bootloader->err = C_BTLDR_ERR_FILE;
		goto out;
	}

	tfm = crypto_alloc_shash("md5", 0, 0);
	if (IS_ERR(tfm)) {
		printk(KERN_ERR "Failed to load transform for md5\n");
		bootloader->err = C_BTLDR_ERR_FILE;
		goto out;
	}

	do {
		SHASH_DESC_ON_STACK(shash, tfm);
		shash->tfm = tfm;
		shash->flags = 0;

		crypto_shash_init(shash);
		crypto_shash_update(shash, content, cyacd_size);
		crypto_shash_final(shash, bootloader->cyacd_md5_digest);

	} while (0);

	crypto_free_shash(tfm);
	bootloader->err = C_BTLDR_SUCCESS;

out:
	return bootloader->err;
}
EXPORT_SYMBOL(bldc_open_cyacd_file);


