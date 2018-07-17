/**
 ************************************************
 *@file bldc_host_bootloader_routines.c
 *@brief Servo firmware loading routines.
 *
 *Copyright (C) 2015 Parrot S.A.
 *
 *@author Quentin QUADRAT <quentin.quadrat@parrot.com>
 *@author Karl Leplat <karl.lepat@parrot.com>
 *@date 2015-06-22
 *************************************************
 */

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "bldc_host_bootloader_routines.h"

#define C_FAIL        0
#define C_SUCCESS     1
#define C_IN_PROGRESS 2

/* Maximum number of bytes to allocate for a single command.  */
#define C_BTLDR_MAX_COMMAND_SIZE        512
/* The first byte of any boot loader command. */
#define C_BTLDR_CMD_START               0x01
/* The last byte of any boot loader command. */
#define C_BTLDR_CMD_STOP                0x17
/* The minimum number of bytes in a bootloader command. */
#define C_BTLDR_BASE_CMD_SIZE           0x07
/* The default value if a flash array has not yet received data */
#define C_BTLDR_NO_FLASH_ARRAY_DATA     0
/* Checksum type is a basic inverted summation of all bytes */

/* Command identifier for verifying the checksum value
 * of the bootloadable project. */
#define C_BTLDR_CMD_VERIFY_CHECKSUM     0x31
/* Command identifier for getting the number of flash
 * rows in the target device. */
#define C_BTLDR_CMD_GET_FLASH_SIZE      0x32
/* Command identifier for getting info about the app status.
 * This is only supported on multi app bootloader. */
#define C_BTLDR_CMD_GET_APP_STATUS      0x33
/* Command identifier for reasing a row of flash data from the target device. */
#define C_BTLDR_CMD_ERASE_ROW           0x34
/* Command identifier for making sure the bootloader
 * host and bootloader are in sync. */
#define C_BTLDR_CMD_SYNC                0x35
/* Command identifier for setting the active application.
 * This is only supported on multi app bootloader. */
#define C_BTLDR_CMD_SET_ACTIVE_APP      0x36
/* Command identifier for sending a block of data
 * to the bootloader without doing anything with it yet. */
#define C_BTLDR_CMD_SEND_DATA           0x37
/* Command identifier for starting the boot loader.
 * All other commands ignored until this is sent. */
#define C_BTLDR_CMD_ENTER_BOOTLOADER    0x38
/* Command identifier for programming a single row of flash. */
#define C_BTLDR_CMD_PROGRAM_ROW         0x39
/* Command identifier for verifying the contents of a single row of flash. */
#define C_BTLDR_CMD_VERIFY_ROW          0x3A
/* Command identifier for exiting the bootloader
 * and restarting the target program. */
#define C_BTLDR_CMD_EXIT_BOOTLOADER     0x3B

/* Command identifier for writing md5 in bldc flash */
#define C_BTLDR_CMD_WRITE_MD5           0x90
/* Command identifier for writing md5 in bldc flash */
#define C_BTLDR_CMD_READ_MD5            0x91

/* Milliseconds of pause after a write
 * command to let the Cypress performs its action. */
/* Duration in milliseconds needed by Cypress to interprete message */
#define C_BTLDR_MS_DELAY_WRITE		(1U)
/* Duration in milliseconds needed by Cypress to write its flash memory */
#define C_BTLDR_MS_DELAY_FLASH		(10U)
/* Duration in milliseconds needed by Cypress to write its E²PROM memory */
#define C_BTLDR_MS_DELAY_MD5		(20U)

/* Define the maximum amount of bytes than can be send by the I2C link */
#define C_COMM_MAX_TRANSFER_SIZE	(128)

/* Milliseconds of pause after the end bootloader command. To let the
 * Cypress jump into the applicative flash (bootloadbale application)
 * and init its hardware (I2C comm). So, avoid to send message on I2C
 * if the slave is not yet ready ! */
#define C_BTLDR_MS_DELAY_WAKEUP         (500UL)

static uint8_t in_buf[C_BTLDR_MAX_COMMAND_SIZE];
static uint8_t out_buf[C_BTLDR_MAX_COMMAND_SIZE];

/* ***********************************************************************
 * Write x bytes in the I2C device and then make a delay.
 * Mainly used for bootloader.
 * Call HAL_bldc_write and make a pause of x milliseconds.
 * @param buffer_tx (IN) address of buffer where data are stored.
 * Shall be allocated before.
 * @param size (IN) number of bytes to read. This number shall be less
 * than the size of buffer_tx.
 * No check are performed on to verify array overflow.
 * @param msdelay (IN) number of milliseconds of pause after the write.
 * @return C_FAIL if not all bytes are written. C_SUCCESS else.
 * ***********************************************************************/
static int bldc_comm_write_and_pause(struct i2c_client *client,
		const uint8_t *const buffer_tx,
		const uint16_t size,
		const uint32_t msdelay)
{
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = (char *)buffer_tx;
	msg.len = size;

	ret = i2c_transfer(client->adapter,
			&msg,
			1);

	usleep_range(msdelay * 1000, (msdelay * 1000) + 25);
	return ret == 1 ? C_SUCCESS : C_FAIL;
}

static int bldc_comm_read_for_bootloader(struct i2c_client *client,
			uint8_t *const buffer_rx,
			const uint16_t size)
{
	struct timeval        start_time, tv;
	int                   res, r;
	int32_t               i, j;
	int32_t               remaining = (int32_t) size;
	int32_t               total = 0;
	int32_t               stx_found = 0;
	struct i2c_msg msg;
	long ms;

	do_gettimeofday(&start_time);

	do {
		msg.addr = client->addr;
		msg.flags = client->flags | I2C_M_RD;
		msg.buf = &buffer_rx[total];
		msg.len = remaining;

		r = i2c_transfer(client->adapter,
				&msg,
				1);
		if (r != 1) {
			dev_err(&client->dev, "I2C transfer failed\n");
			return C_FAIL;
		} else {
			/* Remaining bytes to read before considering
			 * the message is fully readen */
			res = remaining;

			/* If busy, the Cypress sends 0xFF these bytes shall
			 * not be taken into account.
			 * But once the Cypress has sent the byte
			 * 'start of message' (0x01), 0xFF
			 * shall not be ignored. */
			i = 0;
			if (0 == stx_found) {
				/* The STX byte (0x01 byte) has not been found.
				 * Ignore 0xFF until 0x01 */
				while ((i < res)
					&& (0xFF == buffer_rx[total + i]))
					++i;

				/* Found the beginning of a Cypress message.
				 * Do not ignore 0xFF bytes */
				if (0x01 == buffer_rx[total + i])
					stx_found = 1;

				/* Translate bytes to the 1st position
				 * the number of 0xFF found. */
				res = res - i;
				j = 0;
				while (j < res) {
					buffer_rx[total + j] =
							buffer_rx[total + i];
					++i;
					++j;
				}
			} else {
				/* Do nothing. bytes shall */
			}
		}
		/* Did we read all 'size' bytes ? */
		remaining = remaining - res;
		total = total + res;

		/* Avoid infinite loop by timeout */
		do_gettimeofday(&tv);
		ms = (tv.tv_sec - start_time.tv_sec) * 1000 +
			(tv.tv_usec - start_time.tv_usec) / 1000;

		if (ms >= 1000) {
			dev_err(&client->dev, "timeout (%ld)\n", ms);
			return C_FAIL;
		}


	} while (remaining > 0);

	return C_SUCCESS;
}

/*******************************************************************************
 * Function Name: transfer_data_fix_delay
 *******************************************************************************
 * Summary:
 *   This function is responsible for transfering a buffer of data to the target
 *   device and then reading a response packet back from the device.
 *
 * Parameters:
 *   in_buf   - The buffer containing data to send to the target device
 *   in_size  - The number of bytes to send to the target device
 *   out_buf  - The buffer to store the data read from the device
 *   out_size - The number of bytes to read from the target device
 *
 * Return:
 *   C_BTLDR_SUCCESS  - The transfer completed successfully
 *   C_BTLDR_ERR_COMM - There was a communication error talking to the device
 *
 ******************************************************************************/
static uint8_t transfer_data(struct i2c_client *client,
		const uint8_t *const in_buf,
		const uint16_t in_size,
		uint8_t *const out_buf,
		const uint16_t out_size,
		const uint32_t delay)
{
	int16_t         n;
	uint8_t err = C_BTLDR_SUCCESS;

	n = bldc_comm_write_and_pause(client, in_buf, in_size, delay);
	if (n != C_SUCCESS) {
		dev_err(&client->dev, "bldc_comm_write_and_pause failed\n");
		err = C_BTLDR_ERR_COMM_MASK;
	} else {
		n = bldc_comm_read_for_bootloader(client, out_buf, out_size);

		if (n != C_SUCCESS) {
			dev_err(&client->dev,
				"bldc_comm_read_for_bootloader failed\n");
			err = C_BTLDR_ERR_COMM_MASK;
		}
	}

	return err;
}

#define transfer_data_fix_delay(client, in_buf, in_size, out_buf, out_size) \
	transfer_data(client, in_buf, in_size, \
			out_buf, out_size, C_BTLDR_MS_DELAY_WRITE);
#define transfer_data_delay(client, in_buf, in_size, out_buf, out_size, delay) \
	transfer_data(client, in_buf, in_size, out_buf, out_size, delay);

/*******************************************************************************
 * Function Name: from_hex
 *******************************************************************************
 * Summary:
 *   Converts the provided ASCII char into its hexadecimal numerical equivilant.
 *
 * Parameters:
 *   value - the ASCII char to convert into a number
 *
 * Return:
 *   The hexadecimal numerical equivilant of the provided ASCII char.  If the
 *   provided char is not a valid ASCII char, it will return 0.
 *
 ******************************************************************************/
static uint8_t from_hex(const char value)
{
	if (('0' <= value) && (value <= '9'))
		return (uint8_t) (value - '0');
	if (('a' <= value) && (value <= 'f'))
		return (uint8_t) (10 + value - 'a');
	if (('A' <= value) && (value <= 'F'))
		return (uint8_t) (10 + value - 'A');
	return 0;
}

/*******************************************************************************
 * Function Name: from_ascii
 *******************************************************************************
 * Summary:
 *   Converts the provided ASCII array into its hexadecimal numerical equivilant
 *
 * Parameters:
 *   buf_size - The length of the buffer to convert
 *   buffer  - The buffer of ASCII characters to convert
 *   row_size - The number of bytes of equivilant hex data generated
 *   row_data - The hex data generated for the buffer
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The buffer was converted successfully
 *   C_BTLDR_ERR_LENGTH - The buffer does not have an even number of chars
 *
 ******************************************************************************/
static uint8_t from_ascii(const uint16_t buf_size,
		const uint8_t *const buffer,
		uint16_t *const row_size,
		uint8_t *const row_data)
{
	uint16_t i;
	uint8_t err = C_BTLDR_SUCCESS;

	if (buf_size & 1)
		err = C_BTLDR_ERR_LENGTH;
	else {
		for (i = 0; i < buf_size / 2; i++)
			row_data[i] = (from_hex(buffer[i * 2]) << 4)
					| from_hex(buffer[i * 2 + 1]);
		*row_size = i;
	}

	return err;
}

/*******************************************************************************
 * Function Name: compute_checksum
 *******************************************************************************
 * Summary:
 *   Computes the 2byte checksum for the provided command data.  The checksum is
 *   the 2's complement of the 1-byte sum of all bytes.
 *
 * Parameters:
 *   buf  - The data to compute the checksum on
 *   size - The number of bytes contained in buf.
 *
 * Return:
 *   The checksum for the provided data.
 *
 ******************************************************************************/
static uint16_t compute_checksum(const uint8_t *const buffer,
		const uint16_t s,
		const uint8_t chksum_type)
{
	uint16_t size = s;
	const uint8_t *buf = buffer;

	if (C_BTLDR_CRC_CHECKSUM == chksum_type) {
		uint16_t crc = 0xffff;
		uint16_t tmp;
		int i;

		if (size == 0)
			return ~crc;

		do {
			for (i = 0, tmp = 0x00ff & *buf++; i < 8; i++,
				tmp >>= 1) {
				if ((crc & 0x0001) ^ (tmp & 0x0001))
					crc = (crc >> 1) ^ 0x8408;
				else
					crc >>= 1;
			}
		} while (--size);

		crc = ~crc;
		tmp = crc;
		crc = (crc << 8) | (tmp >> 8 & 0xFF);

		return crc;
	} else {
		uint16_t sum = 0;
		while (size-- > 0)
			sum += *buf++;

		return 1 + ~sum;
	}
}

/*******************************************************************************
 * Function Name: parse_default_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from any command that returns the default result packet
 *   data.  The default result is just a status byte
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   status  - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_default_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const status)
{
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_BTLDR_BASE_CMD_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != 0 ||
			cmd_buf[3] != 0 ||
			cmd_buf[6] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else
		*status = cmd_buf[1];

	return err;
}

/*******************************************************************************
 * Function Name: create_enter_bootloader_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to startup the bootloader.
 *   NB: This command must be sent before the bootloader will respond to any
 *       other command.
 *
 * Parameters:
 *   protect - The flash protection settings.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_enter_bootloader_cmd(uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_RESULT_DATA_SIZE = 8;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE + C_RESULT_DATA_SIZE;
	*cmd_size = C_BTLDR_BASE_CMD_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_ENTER_BOOTLOADER;
	cmd_buf[2] = 0;
	cmd_buf[3] = 0;
	checksum = compute_checksum(cmd_buf, C_BTLDR_BASE_CMD_SIZE - 3,
			chksum_type);
	cmd_buf[4] = (uint8_t) checksum;
	cmd_buf[5] = (uint8_t) (checksum >> 8);
	cmd_buf[6] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_enter_bootloader_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the EnterBootLoader command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf     - The buffer containing the output from the bootloader.
 *   cmd_size    - The number of bytes in cmd_buf.
 *   silicon_id  - The silicon ID of the device being communicated with.
 *   silicon_rev - The silicon Revision of the device being communicated with.
 *   bl_version  - The bootloader version being communicated with.
 *   status     - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_enter_bootloader_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint32_t *const silicon_id,
		uint32_t *const silicon_rev,
		uint32_t *const bl_version,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 8;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		*silicon_id = (cmd_buf[7] << 24) | (cmd_buf[6] << 16)
				| (cmd_buf[5] << 8) | cmd_buf[4];
		*silicon_rev = cmd_buf[8];
		*bl_version = (cmd_buf[11] << 16)
				| (cmd_buf[10] << 8) | cmd_buf[9];
		*status = cmd_buf[1];
	}

	return err;
}

/*******************************************************************************
 * Function Name: create_exit_bootloader_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to stop communicating with the boot loader and to
 *   trigger the target device to restart, running the new bootloadable
 *   application.
 *
 * Parameters:
 *   reset_type - The type of reset to perform (0 = Reset, 1 = Direct Call).
 *   cmd_buf    - The preallocated buffer to store command data in.
 *   cmd_size   - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_exit_bootloader_cmd(const uint32_t reset_type,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_COMMAND_DATA_SIZE = 1;
	const uint8_t C_COMMAND_SIZE = C_BTLDR_BASE_CMD_SIZE
			+ C_COMMAND_DATA_SIZE;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE;
	*cmd_size = C_COMMAND_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_EXIT_BOOTLOADER;
	cmd_buf[2] = (uint8_t) C_COMMAND_DATA_SIZE;
	cmd_buf[3] = (uint8_t) (C_COMMAND_DATA_SIZE >> 8);
	cmd_buf[4] = reset_type;
	checksum = compute_checksum(cmd_buf, C_COMMAND_SIZE - 3, chksum_type);
	cmd_buf[5] = (uint8_t) checksum;
	cmd_buf[6] = (uint8_t) (checksum >> 8);
	cmd_buf[7] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: create_program_row_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to program a single flash row.
 *
 * Parameters:
 *   array_id - The array id to program.
 *   row_num  - The row number to program.
 *   buf     - The buffer of data to program into the flash row.
 *   size    - The number of bytes in data for the row.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_program_row_cmd(const uint8_t array_id,
		const uint16_t row_num,
		const uint8_t *const buf,
		const uint16_t size,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_COMMAND_DATA_SIZE = 3;
	uint16_t checksum, i;

	*res_size = C_BTLDR_BASE_CMD_SIZE;
	*cmd_size = C_BTLDR_BASE_CMD_SIZE + C_COMMAND_DATA_SIZE + size;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_PROGRAM_ROW;
	cmd_buf[2] = (uint8_t) (size + C_COMMAND_DATA_SIZE);
	cmd_buf[3] = (uint8_t) ((size + C_COMMAND_DATA_SIZE) >> 8);
	cmd_buf[4] = array_id;
	cmd_buf[5] = (uint8_t) row_num;
	cmd_buf[6] = (uint8_t) (row_num >> 8);
	for (i = 0; i < size; i++)
		cmd_buf[i + 7] = buf[i];

	checksum = compute_checksum(cmd_buf, (*cmd_size) - 3, chksum_type);
	cmd_buf[*cmd_size - 3] = (uint8_t) checksum;
	cmd_buf[*cmd_size - 2] = (uint8_t) (checksum >> 8);
	cmd_buf[*cmd_size - 1] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_program_row_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the ProgramRow command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   status  - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_program_row_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const status)
{
	return parse_default_cmd_result(cmd_buf, cmd_size, status);
}

/*******************************************************************************
 * Function Name: create_verify_row_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to verify that the contents of flash match the
 *   provided row data.
 *
 * Parameters:
 *   array_id - The array id to verify.
 *   row_num  - The row number to verify.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_verify_row_cmd(const uint8_t array_id,
		const uint16_t row_num,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_RESULT_DATA_SIZE = 1;
	const uint8_t C_COMMAND_DATA_SIZE = 3;
	const uint8_t C_COMMAND_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_COMMAND_DATA_SIZE;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE + C_RESULT_DATA_SIZE;
	*cmd_size = C_COMMAND_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_VERIFY_ROW;
	cmd_buf[2] = (uint8_t) C_COMMAND_DATA_SIZE;
	cmd_buf[3] = (uint8_t) (C_COMMAND_DATA_SIZE >> 8);
	cmd_buf[4] = array_id;
	cmd_buf[5] = (uint8_t) row_num;
	cmd_buf[6] = (uint8_t) (row_num >> 8);
	checksum = compute_checksum(cmd_buf, C_COMMAND_SIZE - 3, chksum_type);
	cmd_buf[7] = (uint8_t) checksum;
	cmd_buf[8] = (uint8_t) (checksum >> 8);
	cmd_buf[9] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_verify_row_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the VerifyRow command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf   - The preallocated buffer to store command data in.
 *   cmd_size  - The number of bytes in the command.
 *   checksum - The checksum from the row to verify.
 *   status   - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_verify_row_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const checksum,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 1;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		*checksum = cmd_buf[4];
		*status = cmd_buf[1];
	}

	return err;
}

/*******************************************************************************
 * Function Name: create_erase_row_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to erase a single flash row.
 *
 * Parameters:
 *   array_id - The array id to erase.
 *   row_num  - The row number to erase.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_erase_row_cmd(const uint8_t array_id,
		const uint16_t row_num,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_COMMAND_DATA_SIZE = 3;
	const uint8_t C_COMMAND_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_COMMAND_DATA_SIZE;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE;
	*cmd_size = C_COMMAND_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_ERASE_ROW;
	cmd_buf[2] = (uint8_t) C_COMMAND_DATA_SIZE;
	cmd_buf[3] = (uint8_t) (C_COMMAND_DATA_SIZE >> 8);
	cmd_buf[4] = array_id;
	cmd_buf[5] = (uint8_t) row_num;
	cmd_buf[6] = (uint8_t) (row_num >> 8);
	checksum = compute_checksum(cmd_buf, C_COMMAND_SIZE - 3, chksum_type);
	cmd_buf[7] = (uint8_t) checksum;
	cmd_buf[8] = (uint8_t) (checksum >> 8);
	cmd_buf[9] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_erase_row_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the EraseRow command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   status  - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_erase_row_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const status)
{
	return parse_default_cmd_result(cmd_buf, cmd_size, status);
}

/*******************************************************************************
 * Function Name: create_verify_checksum_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to verify that the checkusm value in flash matches
 *   what is expected.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_verify_checksum_cmd(uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_RESULT_DATA_SIZE = 1;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE + C_RESULT_DATA_SIZE;
	*cmd_size = C_BTLDR_BASE_CMD_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_VERIFY_CHECKSUM;
	cmd_buf[2] = 0;
	cmd_buf[3] = 0;
	checksum = compute_checksum(cmd_buf, C_BTLDR_BASE_CMD_SIZE - 3,
				chksum_type);
	cmd_buf[4] = (uint8_t) checksum;
	cmd_buf[5] = (uint8_t) (checksum >> 8);
	cmd_buf[6] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: create_read_md5_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to read md5 hash stored in bldc internal flash.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_read_md5_cmd(uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_RESULT_DATA_SIZE = 16;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE + C_RESULT_DATA_SIZE;
	*cmd_size = C_BTLDR_BASE_CMD_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_READ_MD5;
	cmd_buf[2] = 0;
	cmd_buf[3] = 0;
	checksum = compute_checksum(cmd_buf, C_BTLDR_BASE_CMD_SIZE - 3,
				chksum_type);
	cmd_buf[4] = (uint8_t) checksum;
	cmd_buf[5] = (uint8_t) (checksum >> 8);
	cmd_buf[6] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: create_write_md5_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to read md5 hash stored in bldc internal flash.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_write_md5_cmd(uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type,
		const uint8_t *md5)
{
	const uint8_t C_COMMAND_DATA_SIZE = 16;
	const uint8_t C_COMMAND_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_COMMAND_DATA_SIZE;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE;
	*cmd_size = C_COMMAND_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_WRITE_MD5;
	cmd_buf[2] = (uint8_t) C_COMMAND_DATA_SIZE;
	cmd_buf[3] = (uint8_t) (C_COMMAND_DATA_SIZE >> 8);
	memcpy(&cmd_buf[4], md5, C_COMMAND_DATA_SIZE);
	checksum = compute_checksum(cmd_buf, C_COMMAND_SIZE - 3, chksum_type);
	cmd_buf[4 + C_COMMAND_DATA_SIZE] = (uint8_t) checksum;
	cmd_buf[5 + C_COMMAND_DATA_SIZE] = (uint8_t) (checksum >> 8);
	cmd_buf[6 + C_COMMAND_DATA_SIZE] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_verify_checksum_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the VerifyChecksum command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf   - The preallocated buffer to store command data in.
 *   cmd_size  - The number of bytes in the command.
 *   checksum_valid - Whether or not the full checksums match
 *			(1 = valid, 0 = invalid)
 *   status   - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_verify_checksum_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const checksum_valid,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 1;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		*checksum_valid = cmd_buf[4];
		*status = cmd_buf[1];
	}

	return err;
}
/*******************************************************************************
 * Function Name: parse_verify_read_md5_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the read md5 command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf   - The preallocated buffer to store command data in.
 *   cmd_size  - The number of bytes in the command.
 *   md5       - md5 hash result
 *   status    - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_verify_read_md5_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *md5, size_t size,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 16;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		/* get md5sum and status */
		memcpy(md5, &cmd_buf[4], min((size_t)C_RESULT_DATA_SIZE, size));
		*status = cmd_buf[1];
	}

	return err;
}

/*******************************************************************************
 * Function Name: parse_verify_write_md5_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the read md5 command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf   - The preallocated buffer to store command data in.
 *   cmd_size  - The number of bytes in the command.
 *   md5       - md5 hash result
 *   status    - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_verify_write_md5_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 0;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		/* get status */
		*status = cmd_buf[1];
	}

	return err;
}
/*******************************************************************************
 * Function Name: create_get_flash_size_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to retreive the number of flash rows in the device
 *
 * Parameters:
 *   array_id - The array ID to get the flash size of.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_get_flash_size_cmd(const uint8_t array_id,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	const uint8_t C_RESULT_DATA_SIZE = 4;
	const uint8_t C_COMMAND_DATA_SIZE = 1;
	const uint8_t C_COMMAND_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_COMMAND_DATA_SIZE;
	uint16_t checksum;

	*res_size = C_BTLDR_BASE_CMD_SIZE + C_RESULT_DATA_SIZE;
	*cmd_size = C_COMMAND_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_GET_FLASH_SIZE;
	cmd_buf[2] = (uint8_t) C_COMMAND_DATA_SIZE;
	cmd_buf[3] = (uint8_t) (C_COMMAND_DATA_SIZE >> 8);
	cmd_buf[4] = array_id;
	checksum = compute_checksum(cmd_buf, C_COMMAND_SIZE - 3, chksum_type);
	cmd_buf[5] = (uint8_t) checksum;
	cmd_buf[6] = (uint8_t) (checksum >> 8);
	cmd_buf[7] = C_BTLDR_CMD_STOP;
}

/*******************************************************************************
 * Function Name: parse_get_flash_size_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the GetFlashSize command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf   - The preallocated buffer to store command data in.
 *   cmd_size  - The number of bytes in the command.
 *   start_row - The first available row number in the flash array.
 *   end_row   - The last available row number in the flash array.
 *   status   - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_get_flash_size_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint16_t *const start_row,
		uint16_t *const end_row,
		uint8_t *const status)
{
	const uint8_t C_RESULT_DATA_SIZE = 4;
	const uint8_t C_RESULT_SIZE = C_BTLDR_BASE_CMD_SIZE
					+ C_RESULT_DATA_SIZE;
	uint8_t err = C_BTLDR_SUCCESS;

	if (cmd_size != C_RESULT_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (cmd_buf[1] != C_BTLDR_SUCCESS)
		err = C_BTLDR_ERR_BTLDR_MASK | (*status = cmd_buf[1]);
	else if (cmd_buf[0] != C_BTLDR_CMD_START ||
			cmd_buf[2] != C_RESULT_DATA_SIZE ||
			cmd_buf[3] != (C_RESULT_DATA_SIZE >> 8) ||
			cmd_buf[C_RESULT_SIZE - 1] != C_BTLDR_CMD_STOP)
		err = C_BTLDR_ERR_DATA;
	else {
		*start_row = (cmd_buf[5] << 8) | cmd_buf[4];
		*end_row = (cmd_buf[7] << 8) | cmd_buf[6];
		*status = cmd_buf[1];
	}

	return err;
}

/*******************************************************************************
 * Function Name: create_send_data_cmd
 *******************************************************************************
 * Summary:
 *   Creates the command used to send a block of data to the target.
 *
 * Parameters:
 *   buf     - The buffer of data data to program into the flash row.
 *   size    - The number of bytes in data for the row.
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   res_size - The number of bytes expected in the bootloader's response packet
 *
 ******************************************************************************/
static void create_send_data_cmd(const uint8_t *const buf,
		const uint16_t size,
		uint8_t *const cmd_buf,
		uint16_t *const cmd_size,
		uint16_t *const res_size,
		const uint8_t chksum_type)
{
	uint16_t checksum, i;

	*res_size = C_BTLDR_BASE_CMD_SIZE;
	*cmd_size = size + C_BTLDR_BASE_CMD_SIZE;
	cmd_buf[0] = C_BTLDR_CMD_START;
	cmd_buf[1] = C_BTLDR_CMD_SEND_DATA;
	cmd_buf[2] = (uint8_t) size;
	cmd_buf[3] = (uint8_t) (size >> 8);
	for (i = 0; i < size; i++)
		cmd_buf[i + 4] = buf[i];

	checksum = compute_checksum(cmd_buf, (*cmd_size) - 3, chksum_type);
	cmd_buf[(*cmd_size) - 3] = (uint8_t) checksum;
	cmd_buf[(*cmd_size) - 2] = (uint8_t) (checksum >> 8);
	cmd_buf[(*cmd_size) - 1] = C_BTLDR_CMD_STOP;
}


/*******************************************************************************
 * Function Name: parse_send_data_cmd_result
 *******************************************************************************
 * Summary:
 *   Parses the output from the SendData command to get the resultant
 *   data.
 *
 * Parameters:
 *   cmd_buf  - The preallocated buffer to store command data in.
 *   cmd_size - The number of bytes in the command.
 *   status  - The status code returned by the bootloader.
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The command was constructed successfully
 *   C_BTLDR_ERR_LENGTH - The packet does not contain enough data
 *   C_BTLDR_ERR_DATA   - The packet's contents are not correct
 *
 ******************************************************************************/
static uint8_t parse_send_data_cmd_result(const uint8_t *const cmd_buf,
		const uint16_t cmd_size,
		uint8_t *const status)
{
	return parse_default_cmd_result(cmd_buf, cmd_size, status);
}

/*******************************************************************************
 * Function Name: bldc_btldr_validate_row
 *******************************************************************************
 * Summary:
 *   This function is responsible for verifying that the provided array_id and
 *   row number are valid for a bootload operation.
 *
 * Parameters:
 *   array_id - The array to check
 *   row_num  - The row number within the array to check
 *
 * Return:
 *   C_BTLDR_SUCCESS   - The array and row are available for communication
 *   C_BTLDR_ERR_ARRAY - The array is not valid for communication
 *   C_BTLDR_ERR_ROW   - The array/row number is not valid for communication
 *
 ******************************************************************************/
static uint8_t bldc_btldr_validate_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t chksum_type)
{
	uint16_t in_size;
	uint16_t out_size;
	uint16_t min_row = 0;
	uint16_t max_row = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err = C_BTLDR_SUCCESS;

	if (array_id < C_BTLDR_MAX_FLASH_ARRAYS) {
		if (C_BTLDR_NO_FLASH_ARRAY_DATA == valid_rows[array_id]) {
			create_get_flash_size_cmd(array_id, in_buf, &in_size,
						&out_size, chksum_type);
			err = transfer_data_fix_delay(client, in_buf, in_size,
							out_buf,
							out_size);
			if (C_BTLDR_SUCCESS == err)
				err = parse_get_flash_size_cmd_result(out_buf,
						out_size,
						&min_row,
						&max_row,
						&status);
			if (C_BTLDR_SUCCESS != status)
				err = status | C_BTLDR_ERR_BTLDR_MASK;
			if (C_BTLDR_SUCCESS == err) {
				if (C_BTLDR_SUCCESS == status)
					valid_rows[array_id] = (min_row << 16)
								+ max_row;
				else
					err = status | C_BTLDR_ERR_BTLDR_MASK;
			}
		}
		if (C_BTLDR_SUCCESS == err) {
			min_row = (uint16_t) (valid_rows[array_id] >> 16);
			max_row = (uint16_t) valid_rows[array_id];
			if (row_num < min_row || row_num > max_row)
				err = C_BTLDR_ERR_ROW;
		}
	} else
		err = C_BTLDR_ERR_ARRAY;

	return err;
}

uint8_t bldc_btldr_send_start_bootload_operation(struct i2c_client *client,
		uint32_t *valid_rows,
		const uint8_t chksum_type)
{
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint16_t i;

	for (i = 0; i < C_BTLDR_MAX_FLASH_ARRAYS; i++)
		valid_rows[i] = C_BTLDR_NO_FLASH_ARRAY_DATA;

	create_enter_bootloader_cmd(in_buf, &in_size, &out_size, chksum_type);
	return transfer_data_fix_delay(client, in_buf, in_size, out_buf,
					out_size);
}

/*******************************************************************************
 * Function Name: bldc_btldr_start_bootload_operation
 *******************************************************************************
 * Summary:
 *   Initiates a new bootload operation.  This must be called before any other
 *   request to send data to the bootloader.  A corresponding call to
 *   bldc_btldr_end_bootloadoperation() should be made once all transactions are
 *   complete.
 *
 * Parameters:
 *   comm   - Communication struct used for communicating with the target device
 *   exp_si_id-The expected Silicon ID of the device (read from the cyacd file)
 *   exp_si_rev-The expected Silicon Rev of the device :read from the cyacd file
 *   silicon_id  - The silicon ID given by the device.
 *   silicon_rev - The silicon Revision given by the device.
 *   bootVer    - The Bootloader version that is running on the device
 *
 * Return:
 *   C_BTLDR_SUCCESS     - The start request was sent successfully
 *   C_BTLDR_ERR_DEVICE  - The detected device does not match the desired device
 *   C_BTLDR_ERR_VERSION - The detected bootloader version is not compatible
 *   C_BTLDR_ERR_BTLDR   - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM    - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_start_bootload_operation(struct i2c_client *client,
		const uint32_t exp_si_id,
		const uint32_t exp_si_rev,
		uint32_t *const silicon_id,
		uint32_t *const silicon_rev,
		uint32_t *const btldr_version,
		uint32_t *valid_rows,
		const uint8_t chksum_type)
{
	const uint32_t C_SUPPORTED_BOOTLOADER = 0x010000;
	const uint32_t C_BOOTLOADER_VERSION_MASK = 0xFF0000;
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;
	uint16_t i;

	for (i = 0; i < C_BTLDR_MAX_FLASH_ARRAYS; i++)
		valid_rows[i] = C_BTLDR_NO_FLASH_ARRAY_DATA;

	create_enter_bootloader_cmd(in_buf, &in_size, &out_size, chksum_type);
	err = transfer_data_fix_delay(client, in_buf, in_size, out_buf,
					out_size);
	if (C_BTLDR_SUCCESS == err)
		err = parse_enter_bootloader_cmd_result(out_buf,
				out_size,
				silicon_id,
				silicon_rev,
				btldr_version,
				&status);

	if (C_BTLDR_SUCCESS == err) {
		if (C_BTLDR_SUCCESS != status)
			err = status | C_BTLDR_ERR_BTLDR_MASK;
		else if ((exp_si_id != *silicon_id)
			|| (exp_si_rev != *silicon_rev))
			err = C_BTLDR_ERR_DEVICE;
		else if (C_SUPPORTED_BOOTLOADER != (*btldr_version
						& C_BOOTLOADER_VERSION_MASK))
			err = C_BTLDR_ERR_VERSION;
	}

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_end_bootloadoperation
 *******************************************************************************
 * Summary:
 *   Terminates the current bootload operation.  This should be called once all
 *   bootload commands have been sent and no more communication is desired.
 *
 * Parameters:
 *   void.
 *
 * Return:
 *   C_BTLDR_SUCCESS   - The end request was sent successfully
 *   C_BTLDR_ERR_BTLDR - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM  - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_end_bootloadoperation(struct i2c_client *client,
					const uint8_t chksum_type)
{
	const uint8_t C_RESET = 0x00;
	uint16_t in_size;
	uint16_t out_size, n;
	uint8_t err = C_BTLDR_SUCCESS;

	create_exit_bootloader_cmd(C_RESET, in_buf, &in_size,
				&out_size, chksum_type);
	n = bldc_comm_write_and_pause(client, in_buf,
				in_size, C_BTLDR_MS_DELAY_WAKEUP);
	if (n != C_SUCCESS)
		err = C_BTLDR_ERR_COMM_MASK;

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_program_row
 *******************************************************************************
 * Summary:
 *   Sends a single row of data to the bootloader to be programmed into flash
 *
 * Parameters:
 *   array_id - The flash array that is to be reprogrammed
 *   row_num  - The row number within the array that is to be reprogrammed
 *   buf     - The buffer of data to program into the devices flash
 *   size   - The number of bytes in data that need to be sent to the bootloader
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The row was programmed successfully
 *   C_BTLDR_ERR_LENGTH - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA   - The result packet does not contain valid data
 *   C_BTLDR_ERR_ARRAY  - The array is not valid for programming
 *   C_BTLDR_ERR_ROW    - The array/row number is not valid for programming
 *   C_BTLDR_ERR_BTLDR  - The bootloader experienced an error
 *   C_BTLDR_ERR_ACTIVE - The application is currently marked as active
 *
 ******************************************************************************/
uint8_t bldc_btldr_program_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t *const buf,
		const uint16_t size,
		const uint8_t chksum_type)
{
	const uint16_t C_TRANSFER_HEADER_SIZE = 7;
	uint16_t in_size;
	uint16_t out_size;
	uint16_t offset = 0;
	uint16_t sub_buf_size;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	err = bldc_btldr_validate_row(client, array_id, row_num, valid_rows,
					chksum_type);
	if (err != C_BTLDR_SUCCESS)
		dev_err(&client->dev,
			"bldc_btldr_validate_row failed err(%d)\n", err);
	while ((C_BTLDR_SUCCESS == err) &&
			(size > C_COMM_MAX_TRANSFER_SIZE
			- C_TRANSFER_HEADER_SIZE + offset)) {
		sub_buf_size = (uint16_t) (C_COMM_MAX_TRANSFER_SIZE
					- C_TRANSFER_HEADER_SIZE);
		create_send_data_cmd(&buf[offset], sub_buf_size,
					in_buf, &in_size,
					&out_size, chksum_type);
		err = transfer_data_fix_delay(client, in_buf, in_size,
						out_buf, out_size);
		if (err != C_BTLDR_SUCCESS)
			dev_err(&client->dev,
				"multiple flash rows failed err(%d)\n", err);
		if (C_BTLDR_SUCCESS == err)
			err = parse_send_data_cmd_result(out_buf,
							out_size, &status);
		if (C_BTLDR_SUCCESS != status) {
			dev_err(&client->dev,
				"multiple flash rows response failed(%d)\n",
				err);
			err = status | C_BTLDR_ERR_BTLDR_MASK;
		}

		offset += sub_buf_size;
	}

	if (C_BTLDR_SUCCESS == err) {
		sub_buf_size = (uint16_t) (size - offset);
		create_program_row_cmd(array_id, row_num, &buf[offset],
					sub_buf_size, in_buf, &in_size,
					&out_size, chksum_type);
		err = transfer_data_delay(client, in_buf, in_size,
					out_buf, out_size,
					C_BTLDR_MS_DELAY_FLASH);
		if (err != C_BTLDR_SUCCESS)
			dev_err(&client->dev,
				"single flash row failed err(%d)\n", err);
		if (C_BTLDR_SUCCESS == err)
			err = parse_program_row_cmd_result(out_buf,
							out_size, &status);
		if (C_BTLDR_SUCCESS != status) {
			dev_err(&client->dev,
				"single flash rows response failed(%d)\n", err);
			err = status | C_BTLDR_ERR_BTLDR_MASK;
		}
	} else
		dev_err(&client->dev, "program failed err(%d)\n", err);

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_erase_row
 *******************************************************************************
 * Summary:
 *   Erases a single row of flash data from the device.
 *
 * Parameters:
 *   array_id - The flash array that is to have a row erased
 *   row_num  - The row number within the array that is to be erased
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The row was erased successfully
 *   C_BTLDR_ERR_LENGTH - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA   - The result packet does not contain valid data
 *   C_BTLDR_ERR_ARRAY  - The array is not valid for programming
 *   C_BTLDR_ERR_ROW    - The array/row number is not valid for programming
 *   C_BTLDR_ERR_BTLDR  - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM   - There was a communication error talking to the device
 *   C_BTLDR_ERR_ACTIVE - The application is currently marked as active
 *
 ******************************************************************************/
uint8_t bldc_btldr_erase_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t chksum_type)
{
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	err = bldc_btldr_validate_row(client, array_id, row_num, valid_rows,
					chksum_type);
	if (C_BTLDR_SUCCESS == err) {
		create_erase_row_cmd(array_id, row_num, in_buf, &in_size,
					&out_size, chksum_type);
		err = transfer_data_fix_delay(client, in_buf, in_size, out_buf,
						out_size);
	}
	if (C_BTLDR_SUCCESS == err)
		err = parse_erase_row_cmd_result(out_buf, out_size, &status);
	if (C_BTLDR_SUCCESS != status)
		err = status | C_BTLDR_ERR_BTLDR_MASK;

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_verify_row
 *******************************************************************************
 * Summary:
 *   Verifies that the data contained within the specified flash array and row
 *   matches the expected value.
 *
 * Parameters:
 *   array_id  - The flash array that is to be verified
 *   row_num   - The row number within the array that is to be verified
 *   checksum - The expected checksum value for the row
 *
 * Return:
 *   C_BTLDR_SUCCESS      - The row was verified successfully
 *   C_BTLDR_ERR_LENGTH   - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA     - The result packet does not contain valid data
 *   C_BTLDR_ERR_ARRAY        - The array is not valid for programming
 *   C_BTLDR_ERR_ROW      - The array/row number is not valid for programming
 *   C_BTLDR_ERR_CHECKSUM - The checksum does not match the expected value
 *   C_BTLDR_ERR_BTLDR    - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM    - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_verify_row(struct i2c_client *client,
		const uint8_t array_id,
		const uint16_t row_num,
		uint32_t *valid_rows,
		const uint8_t checksum,
		const uint8_t chksum_type)
{
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t row_checksum = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	err = bldc_btldr_validate_row(client, array_id, row_num, valid_rows,
					chksum_type);
	if (C_BTLDR_SUCCESS == err) {
		create_verify_row_cmd(array_id, row_num, in_buf, &in_size,
					&out_size, chksum_type);
		err = transfer_data_fix_delay(client, in_buf, in_size, out_buf,
						out_size);
	}
	if (C_BTLDR_SUCCESS == err)
		err = parse_verify_row_cmd_result(out_buf, out_size,
						&row_checksum, &status);
	if (C_BTLDR_SUCCESS != status)
		err = status | C_BTLDR_ERR_BTLDR_MASK;
	if ((C_BTLDR_SUCCESS == err) && (row_checksum != checksum))
		err = C_BTLDR_ERR_CHECKSUM;

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_verify_application
 *******************************************************************************
 * Summary:
 *   Verifies that the checksum for the entire bootloadable application matches
 *   the expected value.  This is used to verify that the entire bootloadable
 *   image is valid and ready to execute.
 *
 * Parameters:
 *   void
 *
 * Return:
 *   C_BTLDR_SUCCESS      - The application was verified successfully
 *   C_BTLDR_ERR_LENGTH   - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA     - The result packet does not contain valid data
 *   C_BTLDR_ERR_CHECKSUM - The checksum does not match the expected value
 *   C_BTLDR_ERR_BTLDR    - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM    - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_verify_application(struct i2c_client *client,
		const uint8_t chksum_type)
{
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t checksum_valid = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	create_verify_checksum_cmd(in_buf, &in_size, &out_size, chksum_type);
	err = transfer_data_delay(client, in_buf, in_size, out_buf, out_size,
				100);
	if (C_BTLDR_SUCCESS == err)
		err = parse_verify_checksum_cmd_result(out_buf, out_size,
						&checksum_valid, &status);
	if (C_BTLDR_SUCCESS != status)
		err = status | C_BTLDR_ERR_BTLDR_MASK;
	if ((C_BTLDR_SUCCESS == err) && (!checksum_valid))
		err = C_BTLDR_ERR_CHECKSUM;

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_read_md5
 *******************************************************************************
 * Summary:
 *   Read md5sum stored in cypress eeprom
 *
 * Parameters:
 *   void
 *
 * Return:
 *   C_BTLDR_SUCCESS      - The application was verified successfully
 *   C_BTLDR_ERR_LENGTH   - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA     - The result packet does not contain valid data
 *   C_BTLDR_ERR_CHECKSUM - The checksum does not match the expected value
 *   C_BTLDR_ERR_BTLDR    - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM    - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_read_md5(struct i2c_client *client,
		const uint8_t chksum_type, uint8_t *md5, size_t size)
{
	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	create_read_md5_cmd(in_buf, &in_size, &out_size, chksum_type);
	err = transfer_data_fix_delay(client, in_buf, in_size, out_buf,
					out_size);
	if (C_BTLDR_SUCCESS == err)
		err = parse_verify_read_md5_cmd_result(out_buf, out_size,
						md5, size, &status);
	if (C_BTLDR_SUCCESS != status)
		err = status | C_BTLDR_ERR_BTLDR_MASK;

	return err;
}
/*******************************************************************************
 * Function Name: bldc_btldr_read_md5
 *******************************************************************************
 * Summary:
 *   wrtie md5sum in cypress eeprom
 *
 * Parameters:
 *   void
 *
 * Return:
 *   C_BTLDR_SUCCESS      - The application was verified successfully
 *   C_BTLDR_ERR_LENGTH   - The result packet does not have enough data
 *   C_BTLDR_ERR_DATA     - The result packet does not contain valid data
 *   C_BTLDR_ERR_CHECKSUM - The checksum does not match the expected value
 *   C_BTLDR_ERR_BTLDR    - The bootloader experienced an error
 *   C_BTLDR_ERR_COMM    - There was a communication error talking to the device
 *
 ******************************************************************************/
uint8_t bldc_btldr_write_md5(struct i2c_client *client,
		const uint8_t chksum_type, const uint8_t *md5, size_t size)
{

	uint16_t in_size = 0;
	uint16_t out_size = 0;
	uint8_t status = C_BTLDR_SUCCESS;
	uint8_t err;

	create_write_md5_cmd(in_buf, &in_size, &out_size, chksum_type, md5);
	err = transfer_data_delay(client, in_buf, in_size, out_buf, out_size,
				C_BTLDR_MS_DELAY_MD5);
	if (C_BTLDR_SUCCESS == err)
		err = parse_verify_write_md5_cmd_result(out_buf, out_size,
						&status);
	if (C_BTLDR_SUCCESS != status)
		err = status | C_BTLDR_ERR_BTLDR_MASK;

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_parse_header
 *******************************************************************************
 * Summary:
 *   Parses the hader information from the *.cyacd file.  The header information
 *   is stored as the first line, so this method should only be called once,
 *   and only immediatly after calling OpenDataFile and reading the first line.
 *
 * Parameters:
 *   buf_size    - The number of bytes contained within buffer
 *   buffer     - The buffer containing the header data to parse
 *   silicon_id  - The silicon ID that the provided *.cyacd file is for
 *   silicon_rev - The silicon Revision that the provided *.cyacd file is for
 *   chksum_type - The type of checksum to use for packet integrety check
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The file was opened successfully.
 *   C_BTLDR_ERR_LENGTH - The line does not contain enough data
 *
 * Side Effects:
 *    _chksum_type init value.
 *
 ******************************************************************************/
uint8_t bldc_btldr_parse_header(const uint16_t buf_size,
		const uint8_t *const buffer,
		uint32_t *const silicon_id,
		uint32_t *const silicon_rev,
		uint8_t *const chksum_type)
{
	const uint8_t C_LENGTH_ID     = 5;
	const uint8_t C_LENGTH_CHKSUM = C_LENGTH_ID + 1;
	uint16_t row_size;
	uint8_t row_data[C_BTLDR_MAX_BUFFER_SIZE];
	uint8_t err;

	err = from_ascii(buf_size, buffer, &row_size, row_data);
	if (C_BTLDR_SUCCESS == err) {
		if (row_size != C_LENGTH_CHKSUM)
			err = C_BTLDR_ERR_LENGTH;
		else {
			*silicon_id = (uint32_t) (row_data[0] << 24)
				| (row_data[1] << 16)
				| (row_data[2] << 8) | (row_data[3]);
			*silicon_rev = row_data[4];
			*chksum_type = row_data[5];
		}
	}

	return err;
}

/*******************************************************************************
 * Function Name: bldc_btldr_parse_row_data
 *******************************************************************************
 * Summary:
 *   Parses the contents of the provided buffer which is expected to contain
 *   the row data from the *.cyacd file.  This is expected to be called multiple
 *   times.  Once for each row of the *.cyacd file, excluding the header row.
 *
 * Parameters:
 *   buf_size  - The number of bytes contained within buffer
 *   buffer   - The buffer containing the row data to parse
 *   array_id  - The flash array that the row of data belongs in
 *   row_num   - The flash row number that the data corresponds to
 *   row_data  - The preallocated buffer to store the flash row data
 *   size     - The number of bytes of row_data
 *   checksum - The checksum value for the entire row (row_num, size, row_data)
 *
 * Return:
 *   C_BTLDR_SUCCESS    - The file was opened successfully.
 *   C_BTLDR_ERR_LENGTH - The line does not contain enough data
 *   C_BTLDR_ERR_DATA   - The line does not contain a full row of data
 *
 ******************************************************************************/
uint8_t bldc_btldr_parse_row_data(const uint16_t buf_size,
		const uint8_t *const buffer,
		uint8_t *const array_id,
		uint16_t *const row_num,
		uint8_t *const row_data,
		uint16_t *const size,
		uint8_t *const checksum)
{
	const uint8_t C_MIN_SIZE = 6;
	const uint8_t C_DATA_OFFSET = 5;
	uint16_t i, hex_size;
	uint8_t hex_data[C_BTLDR_MAX_BUFFER_SIZE];
	uint8_t err = C_BTLDR_SUCCESS;

	if (buf_size <= C_MIN_SIZE)
		err = C_BTLDR_ERR_LENGTH;
	else if (buffer[0] == ':') {
		err = from_ascii(buf_size - 1, &buffer[1], &hex_size, hex_data);
		if (C_BTLDR_SUCCESS == err) {
			*array_id = hex_data[0];
			*row_num = (hex_data[1] << 8) | (hex_data[2]);
			*size = (hex_data[3] << 8) | (hex_data[4]);
			*checksum = (hex_data[hex_size - 1]);

			if ((*size + C_MIN_SIZE) == hex_size) {
				for (i = 0; i < *size; i++) {
					row_data[i] =
						hex_data[C_DATA_OFFSET + i];
				}
			} else {
				err = C_BTLDR_ERR_DATA;
			}
		}
	} else
		err = C_BTLDR_ERR_DATA;

	return err;
}

