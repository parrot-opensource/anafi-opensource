/**
 * Copyright (c) 2017 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT COMPANY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/delay.h>

#include "smartbattery.h"
#include "smartbattery_misc.h"
#include "smartbattery_common.h"
#include "smartbattery_protocol.h"

#define STATIC_ASSERT(x, msg) typedef char __STATIC_ASSERT__[(x)?1:-1]

#define WAIT_ACTIVE_STEP_MS 100

/* Even if's not really mandatory, we only support that size in API
 * (SMARTBATTERY_CHUNK_SIZE) is the same than in the protocol
 * (SMARTBATTERY_FLASH_BUFFER_SIZE)
 * We could have different value by performing multiple I2C accesses */
STATIC_ASSERT(SMARTBATTERY_FLASH_BUFFER_SIZE == SMARTBATTERY_CHUNK_SIZE, "");

#define BIT_LOG_MSG_DROPPED     (1 << 8)

static uint8_t compute_checksum(const uint8_t *buf, int len)
{
	uint8_t sum = 0;
	int i;

	for (i = 0; i < len; i++)
		sum += buf[i];

	return ~sum;
}

static int request(struct smartbattery_device *device,
	const u8* tx_data, u16 tx_size,
	u8* rx_data, u16 rx_size)
{
	int ret = -EINVAL;
	int nb_send;
	int nb_recv;
	uint8_t sum;
	const char *level = KERN_WARNING;

	if (device->device_is_resetting)
		level = KERN_INFO;

	if ((tx_data == NULL) ||
		(tx_size == 0) ||
		(rx_data == NULL) ||
		(rx_size == 0))
		goto out;

	down(&device->lock);

	nb_send = i2c_master_send(device->client, tx_data, tx_size);
	if (nb_send != tx_size) {
		dev_printk(
			level,
			&device->client->dev,
			"%s(): send error\n", __func__);
		ret = -EIO;
		goto out;
	}

	nb_recv = i2c_master_recv(device->client, rx_data, rx_size);
	if (nb_recv != rx_size) {
		dev_printk(
			level,
			&device->client->dev,
			"%s(): receive error\n", __func__);
		ret = -EIO;
		goto out;
	}
	if (tx_data[0] != rx_data[0]) {
		dev_printk(
			level,
			&device->client->dev,
			"Bad Req ID (0x%x != 0x%x)\n",
			tx_data[0], rx_data[0]);
		ret = -EIO;
		goto out;
	}
	sum = compute_checksum(rx_data, rx_size-1);
	/* checksum always at last position */
	if (sum != rx_data[rx_size-1]) {
		dev_printk(
			level,
			&device->client->dev,
			"Bad Checksum (0x%.4x != 0x%.4x)\n",
			sum, rx_data[rx_size-1]);
		ret = -EIO;
		goto out;
	}

	ret = 0;

out:
	up(&device->lock);
	return ret;
}

int smartbattery_get_magic(
	struct smartbattery_device *device,
	struct smartbattery_magic *magic)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_MAGIC };
	struct get_magic_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	memcpy(magic->value, rx_data.value, sizeof(magic->value));

out:
	return ret;
}

static void get_partition_version(
		struct smartbattery_partition_version* version,
		uint32_t raw_version)
{
	version->major   = raw_version & 0xff;
	version->minor   = raw_version >> 8;
	version->patch   = raw_version >> 16;
	version->variant = raw_version >> 24;
}

int smartbattery_get_version(
		struct smartbattery_device *device,
		struct smartbattery_version *version)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_VERSION };
	struct get_version_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	get_partition_version(&version->application_version,
			rx_data.application_version);
	get_partition_version(&version->updater_version,
			rx_data.updater_version);
	get_partition_version(&version->bootloader_version,
			rx_data.bootloader_version);

	version->application_crc16 = le16toh(rx_data.application_crc16);
	version->updater_crc16 = le16toh(rx_data.updater_crc16);
	version->bootloader_crc16 = le16toh(rx_data.bootloader_crc16);

out:
	return ret;
}

int smartbattery_get_state(
		struct smartbattery_device *device,
		struct smartbattery_state *state)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_STATE };
	struct get_state_rx rx_data;
	uint8_t components;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	state->system_state =  rx_data.state       & 0x3;
	state->power_state  = (rx_data.state >> 2) & 0x3;
	state->drone_state  = (rx_data.state >> 4) & 0x3;
	state->battery_mode = (rx_data.state >> 6) & 0x3;
	components          = (rx_data.state >> 8) & 0xf;
	state->components.ctrl_ok    = components & 1 ? 1 : 0;
	state->components.gauge_ok   = components & 2 ? 1 : 0;
	state->components.charger_ok = components & 4 ? 1 : 0;
	state->components.power_ok   = components & 8 ? 1 : 0;

out:
	return ret;
}

int smartbattery_get_voltage(
		struct smartbattery_device *device,
		struct smartbattery_voltage *voltage)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_VOLTAGE };
	struct get_voltage_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	voltage->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_cell_voltage(
	struct smartbattery_device *device,
	struct smartbattery_cell_voltage *voltage)
{
	int ret;
	struct get_cell_voltage_tx tx_data;
	struct get_cell_voltage_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));

	tx_data.req_id = I2C_REQ_GET_CELL_VOLTAGE;
	tx_data.index = voltage->index;
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	voltage->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_current(
		struct smartbattery_device *device,
		struct smartbattery_current *current_val)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CURRENT };
	struct get_current_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	current_val->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_remaining_cap(
		struct smartbattery_device *device,
		struct smartbattery_capacity *capacity)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_REMAINING_CAP };
	struct get_remaining_cap_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	capacity->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_full_charge_cap(
	struct smartbattery_device *device,
	struct smartbattery_capacity *capacity)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_FULL_CHARGE_CAP };
	struct get_full_charge_cap_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	capacity->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_design_cap(
	struct smartbattery_device *device,
	struct smartbattery_capacity *capacity)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_DESIGN_CAP };
	struct get_design_cap_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	capacity->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_rsoc(
	struct smartbattery_device *device,
	struct smartbattery_rsoc *rsoc)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_RSOC };
	struct get_rsoc_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	rsoc->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_temperature(
	struct smartbattery_device *device,
	struct smartbattery_temperature *temperature)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_TEMPERATURE };
	struct get_temperature_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	temperature->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_charger_temperature(
	struct smartbattery_device *device,
	struct smartbattery_temperature *temperature)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CHARGER_TEMPERATURE };
	struct get_temperature_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	temperature->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_avg_time_to_empty(
	struct smartbattery_device *device,
	struct smartbattery_time *time)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_AVG_TIME_TO_EMPTY };
	struct get_avg_time_to_empty_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	time->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_avg_time_to_full(
	struct smartbattery_device *device,
	struct smartbattery_time *time)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_AVG_TIME_TO_FULL };
	struct get_avg_time_to_full_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	time->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_avg_current(
	struct smartbattery_device *device,
	struct smartbattery_current *current_val)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_AVG_CURRENT };
	struct get_avg_current_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	current_val->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_max_charge_voltage(
	struct smartbattery_device *device,
	struct smartbattery_voltage *voltage)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_MAX_CHARGE_VOLTAGE };
	struct get_max_charge_voltage_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	voltage->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_charging_current(
	struct smartbattery_device *device,
	struct smartbattery_current *current_val)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CHARGING_CURRENT };
	struct get_charging_current_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	current_val->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_get_input_voltage(
	struct smartbattery_device *device,
	struct smartbattery_voltage *voltage)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_INPUT_VOLTAGE };
	struct get_input_voltage_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	voltage->value = le16toh(rx_data.value);

out:
	return ret;
}

int smartbattery_check_flash(
		struct smartbattery_device *device,
		struct smartbattery_check_flash_area *area)
{
	int ret;
	struct check_flash_tx tx_data = {
		.req_id = I2C_REQ_CHECK_FLASH,
		.address = area->address,
		.length = area->length,
	};
	struct check_flash_rx rx_data;

	tx_data.address = htole16(tx_data.address);
	tx_data.length = htole16(tx_data.length);
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);
	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	area->crc16 = le16toh(rx_data.crc16);

out:
	return ret;
}

int smartbattery_erase_flash(
		struct smartbattery_device *device,
		const struct smartbattery_flash_area *area)
{
	int ret;
	struct erase_flash_tx tx_data = {
		.req_id = I2C_CMD_ERASE_FLASH,
		.address = area->address,
		.length = area->length,
	};
	struct erase_flash_rx rx_data;

	tx_data.address = htole16(tx_data.address);
	tx_data.length = htole16(tx_data.length);
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);
	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

int smartbattery_read_flash(
		struct smartbattery_device *device,
		struct smartbattery_flash_chunk *chunk)
{
	int ret;
	struct read_flash_tx tx_data = {
		.req_id = I2C_REQ_READ_FLASH,
		.address = chunk->address,
		.length = chunk->length,
	};
	struct read_flash_rx rx_data;
	int count = 10;

	if (tx_data.length > SMARTBATTERY_FLASH_BUFFER_SIZE)
		tx_data.length = SMARTBATTERY_FLASH_BUFFER_SIZE;
	tx_data.address = htole16(tx_data.address);
	tx_data.length = htole16(tx_data.length);
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	while (count-- > 0) {
		ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
				(uint8_t *)&rx_data, sizeof(rx_data));
		if (ret == 0) {
			chunk->address = le16toh(rx_data.address);
			chunk->length = le16toh(rx_data.length);
			memcpy(chunk->data, rx_data.data, sizeof(rx_data.data));
			break;
		}
		dev_err(&device->client->dev,
				"read flash failed %d\n", ret);
		msleep(10);
	}

	return ret;
}

int smartbattery_get_ctrl_info(
		struct smartbattery_device *device,
		struct smartbattery_ctrl_info *ctrl_info)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CTRL_INFO };
	struct get_ctrl_info_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	ctrl_info->type = rx_data.type;
	switch (ctrl_info->type) {
	case SMARTBATTERY_CTRL_MSP430G2433:
		ctrl_info->type_name = "MSP430G2433";
		break;
	case SMARTBATTERY_CTRL_MSP430G2533:
		ctrl_info->type_name = "MSP430G2533";
		break;
	default:
		ctrl_info->type_name = "unknown";
		break;
	}
	ctrl_info->partitioning_type = rx_data.partitioning_type;
	ctrl_info->partition_count = rx_data.partition_count;
	ctrl_info->led_count = rx_data.led_count;

out:
	return ret;
}

int smartbattery_get_chemistry(
		struct smartbattery_device *device,
		struct smartbattery_chemistry *chemistry)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CHEMISTRY };
	struct get_chemistry_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	chemistry->type = rx_data.type;
	switch (chemistry->type) {
	case SMARTBATTERY_TYPE_LIPO:
		chemistry->type_name = "LiPo";
		break;
	case SMARTBATTERY_TYPE_UNKNOWN:
		chemistry->type_name = "unknown";
		break;
	default:
		chemistry->type_name = "unknown";
		break;
	}

out:
	return ret;
}

int smartbattery_get_cell_config(
	struct smartbattery_device *device,
	struct smartbattery_cell_config *config)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_CELL_CONFIG };
	struct get_cell_config_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	config->code = rx_data.code;
	switch (config->code) {
	case SMARTBATTERY_CELL_CODE_2S1P:
		config->code_name = "2S1P";
		break;
	case SMARTBATTERY_CELL_CODE_UNKNOWN:
		config->code_name = "unknown";
		break;
	default:
		config->code_name = "unknown";
		break;
	}

out:
	return ret;
}

int smartbattery_get_manufacturer(
		struct smartbattery_device *device,
		struct smartbattery_manufacturer *manufacturer)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_MANUFACTURER };
	struct get_manufacturer_rx rx_data;
	int i;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	memcpy(manufacturer->name, rx_data.name,
		SMARTBATTERY_MANUFACTURER_NAME_MAXLEN);
	manufacturer->name[SMARTBATTERY_MANUFACTURER_NAME_MAXLEN] = 0;

	manufacturer->is_empty = 1;
	for (i = 0; i < SMARTBATTERY_MANUFACTURER_NAME_MAXLEN; i++) {
		if ((uint8_t)manufacturer->name[i] != 0xff) {
			manufacturer->is_empty = 0;
			break;
		}
	}

out:
	return ret;
}

int smartbattery_get_device_info(
		struct smartbattery_device *device,
		struct smartbattery_device_info *device_info)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_DEVICE_INFO };
	struct get_device_info_rx rx_data;
	int i;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	memcpy(device_info->name, rx_data.name,
		SMARTBATTERY_DEVICE_NAME_MAXLEN);
	device_info->name[SMARTBATTERY_DEVICE_NAME_MAXLEN] = 0;

	device_info->is_empty = 1;
	for (i = 0; i < SMARTBATTERY_DEVICE_NAME_MAXLEN; i++) {
		if ((uint8_t)device_info->name[i] != 0xff) {
			device_info->is_empty = 0;
			break;
		}
	}

out:
	return ret;
}

int smartbattery_get_hw_version(
		struct smartbattery_device *device,
		struct smartbattery_hw_version *hw_version)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_HW_VERSION };
	struct get_hw_version_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	hw_version->version = rx_data.version;

out:
	return ret;
}

int smartbattery_write_flash(
		struct smartbattery_device *device,
		const struct smartbattery_flash_chunk *chunk)
{
	int ret = -EINVAL;
	struct write_flash_tx tx_data = {
		.req_id = I2C_CMD_WRITE_FLASH,
		.address = chunk->address,
		.length = chunk->length,
	};
	struct write_flash_rx rx_data;
	int count = 10;

	if ((chunk->length & 1) == 1)
		goto out;

	if (tx_data.length > SMARTBATTERY_FLASH_BUFFER_SIZE)
		tx_data.length = SMARTBATTERY_FLASH_BUFFER_SIZE;
	tx_data.address = htole16(tx_data.address);
	tx_data.length = htole16(tx_data.length);
	memcpy(tx_data.data, chunk->data, tx_data.length);
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	while (count-- > 0) {
		ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
				(uint8_t *)&rx_data, sizeof(rx_data));

		if (ret == 0)
			break;
		dev_err(&device->client->dev,
				"Write flash failed %d\n", ret);
		msleep(10);
	}

out:
	return ret;
}

int smartbattery_get_serial(
		struct smartbattery_device *device,
		struct smartbattery_serial *serial)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_SERIAL };
	struct get_serial_rx rx_data;
	int i;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	memcpy(serial->serial_number, rx_data.serial,
		SMARTBATTERY_SERIAL_NUMBER_MAXLEN);
	serial->serial_number[SMARTBATTERY_SERIAL_NUMBER_MAXLEN] = 0;

	serial->is_empty = 1;
	for (i = 0; i < SMARTBATTERY_SERIAL_NUMBER_MAXLEN; i++) {
		if ((uint8_t)serial->serial_number[i] != 0xff) {
			serial->is_empty = 0;
			break;
		}
	}

out:
	return ret;
}

int smartbattery_get_log(
		struct smartbattery_device *device,
		struct smartbattery_log *log)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_LOG };
	struct get_log_rx rx_data;
	uint32_t header;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	header = le32toh(rx_data.header);

	/* we don't really get milliseconds,
	 * but microseconds / 1024
	 * this operation saves space
	 */
	log->time_ms = header >> 10;
	log->msg = header & 0xff;
	log->arg = le16toh(rx_data.arg);

	if (header & BIT_LOG_MSG_DROPPED)
		log->dropped = 1;
	else
		log->dropped = 0;

out:
	return ret;
}

int smartbattery_set_leds(
		struct smartbattery_device *device,
		const struct smartbattery_leds *leds)
{
	int ret;
	int i;
	int j;
	struct set_leds_tx tx_data;
	struct set_leds_rx rx_data;
	uint16_t sequence[SMARTBATTERY_LED_MAX_SEQ];

	memset(&sequence, 0, sizeof(sequence));

	for (j = 0; j < SMARTBATTERY_LED_MAX_SEQ; j++) {
		for (i = SMARTBATTERY_LED_MAX_COUNT; i > 0; i--) {
			sequence[j] <<= 1;
			sequence[j] |= leds->sequence[j][i-1];
		}
	}

	memset(&tx_data, 0, sizeof(tx_data));
	tx_data.req_id = I2C_CMD_SET_LEDS;
	tx_data.mode = leds->mode;
	tx_data.duration = leds->duration;
	for (j = 0; j < SMARTBATTERY_LED_MAX_SEQ; j++)
		tx_data.sequence[j] = sequence[j];

	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
			(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

int smartbattery_set_leds_auto(struct smartbattery_device *device)
{
	struct smartbattery_leds leds;

	memset(&leds, 0, sizeof(leds));

	leds.mode = SMARTBATTERY_LED_MODE_AUTO;

	return smartbattery_set_leds(device, &leds);
}

int smartbattery_reset_to(
		struct smartbattery_device *device,
		const struct smartbattery_reset_type *reset_type)
{
	int ret;
	struct reset_to_tx tx_data;
	struct reset_to_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));

	tx_data.req_id = I2C_CMD_RESET_TO;
	tx_data.partition_type = reset_type->type;
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

static inline int reset_to(
		struct smartbattery_device *device,
		enum smartbattery_partition_type type)
{
	int ret;
	struct smartbattery_reset_type reset_type = {
		.type = type,
	};

	ret = smartbattery_reset_to(device, &reset_type);

	return ret;
}

static const char * const data_role_to_string[] = {
	[SMARTBATTERY_USB_ROLE_HOST] = "host",
	[SMARTBATTERY_USB_ROLE_DEVICE] = "device",
};

static const char *data_role_str(enum smartbattery_usb_data_role role)
{
	const char *ret = "unknown";

	if (role < COUNT_OF(data_role_to_string))
		ret = data_role_to_string[role];

	return ret;
}

static const char * const power_role_to_string[] = {
	[SMARTBATTERY_USB_ROLE_SINK] = "sink",
	[SMARTBATTERY_USB_ROLE_SOURCE] = "source",
};

static const char *power_role_str(enum smartbattery_usb_power_role role)
{
	const char *ret = "unknown";

	if (role < COUNT_OF(power_role_to_string))
		ret = power_role_to_string[role];

	return ret;
}

static void usb_peer_notify(
	struct smartbattery_device *device,
	struct smartbattery_usb_peer *peer)
{
	static bool first = true;
	static bool old_connected;
	static enum smartbattery_usb_data_role old_data_role = UINT_MAX;
	static enum smartbattery_usb_power_role old_power_role = UINT_MAX;
	bool changed = false;

	if (first) {
		dev_info(&device->client->dev,
				"USB is %sconnected",
				peer->connected ? "" : "not ");
		old_connected = peer->connected;
		if (peer->connected) {
			/* role are relevant */
			dev_info(&device->client->dev,
					"USB peer data role %s\n",
					data_role_str(peer->data_role));
			dev_info(&device->client->dev,
					"USB peer power role %s\n",
					power_role_str(peer->power_role));
			old_data_role = peer->data_role;
			old_power_role = peer->power_role;
		}
		first = false;
		return;
	}

	if (old_connected != peer->connected) {
		dev_info(&device->client->dev,
				"USB is %sconnected",
				peer->connected ? "" : "not ");
		old_connected = peer->connected;
		changed = true;
	}

	if (peer->connected) {
		if (old_data_role != peer->data_role) {
			dev_info(&device->client->dev,
					"USB peer data role changed from "
					"%s to %s\n",
					data_role_str(old_data_role),
					data_role_str(peer->data_role));
			old_data_role = peer->data_role;
			changed = true;
		}

		if (old_power_role != peer->power_role) {
			dev_info(&device->client->dev,
					"USB peer power role changed from %s to %s\n",
					power_role_str(old_power_role),
					power_role_str(peer->power_role));
			old_power_role = peer->power_role;
			changed = true;
		}
	}

	/* Callback only when there is a change */
	if (changed && peer->cb) {
		dev_info(&device->client->dev, "Notifying USB\n");
		peer->cb(device);
	}
}

int smartbattery_get_usb_peer(
	struct smartbattery_device *device,
	struct smartbattery_usb_peer *peer)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_USB_PEER };
	struct get_usb_peer_rx rx_data;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret < 0)
		goto out;

	peer->connected = rx_data.connected;
	peer->data_role = rx_data.data_role;
	peer->power_role = rx_data.power_role;

	usb_peer_notify(device, peer);
out:
	return ret;
}

int smartbattery_get_mode(
	struct smartbattery_device *device,
	struct smartbattery_mode *mode)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_MODE };
	struct get_mode_rx rx_data;

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));
	if (ret < 0)
		goto out;

	mode->system_mode = rx_data.system_mode;

out:
	return ret;
}

int smartbattery_set_mode(
	struct smartbattery_device *device,
	const struct smartbattery_mode *mode)
{
	int ret;
	struct set_mode_tx tx_data;
	struct set_mode_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));
	memset(&rx_data, 0, sizeof(rx_data));

	tx_data.req_id = I2C_CMD_SET_MODE;
	tx_data.system_mode = mode->system_mode;
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

int smartbattery_set_usb(
	struct smartbattery_device *device,
	const struct smartbattery_usb *usb)
{
	int ret;
	struct set_usb_tx tx_data;
	struct set_usb_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));
	tx_data.req_id = I2C_CMD_SET_USB;
	tx_data.power_role = usb->power_role;
	tx_data.data_role = usb->data_role;
	tx_data.swap = usb->swap;

	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

int smartbattery_wintering(
	struct smartbattery_device *device,
	const struct smartbattery_wintering *wintering)
{
	int ret;
	struct wintering_tx tx_data;
	struct wintering_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));
	memset(&rx_data, 0, sizeof(rx_data));

	tx_data.req_id = I2C_CMD_WINTERING;
	tx_data.mode = wintering->mode;
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	return ret;
}

int smartbattery_i2c_bridge_async(
	struct smartbattery_device *device,
	struct smartbattery_i2c_request *i2c_request)
{
	int ret;
	size_t count;

	ret = smartbattery_i2c_bridge_request(device, i2c_request);
	if (ret < 0) {
		dev_warn(&device->client->dev,
			"Error in sending I2C Bridge Request\n");
		goto out;
	}

	count = 10;
	/* loop a bit while remote processing is not terminated */
	while (count--) {
		msleep(1);
		ret = smartbattery_i2c_bridge_response(device, i2c_request);
		if (ret == -EBUSY)
			continue;
		if (ret == 0)
			goto out;
		break;
	}
	dev_warn(&device->client->dev, "Bridge Timeout\n");
	ret = -ETIMEDOUT;

out:
	return ret;
}

int smartbattery_i2c_bridge_request(
	struct smartbattery_device *device,
	const struct smartbattery_i2c_request *i2c_request)
{
	int ret;
	struct i2c_request_tx tx_request;
	struct i2c_request_rx rx_request;
	uint8_t buffer[128];
	uint8_t size;

	memset(&tx_request, 0, sizeof(tx_request));
	memset(&rx_request, 0, sizeof(rx_request));

	if (i2c_request->tx_length > sizeof(i2c_request->tx_data)) {
		ret = -EINVAL;
		goto out;
	}
	if (i2c_request->rx_length > sizeof(i2c_request->rx_data)) {
		ret = -EINVAL;
		goto out;
	}

	tx_request.req_id = I2C_CMD_I2C_REQUEST;
	tx_request.size = 3 + i2c_request->tx_length + 3;
	tx_request.component_id = i2c_request->component;
	tx_request.tx_len = i2c_request->tx_length;
	tx_request.rx_len = i2c_request->rx_length;
	memcpy(buffer, &tx_request, sizeof(tx_request));

	memcpy(buffer + sizeof(tx_request), i2c_request->tx_data,
			i2c_request->tx_length);
	size = sizeof(tx_request) + i2c_request->tx_length;
	buffer[size] = compute_checksum(buffer, size);
	size++;

	ret = request(device, buffer, size,
		(uint8_t *)&rx_request, sizeof(rx_request));

out:
	return ret;
}

int smartbattery_i2c_bridge_response(
	struct smartbattery_device *device,
	struct smartbattery_i2c_request *i2c_request)
{
	int ret;
	uint8_t tx_response[] = { I2C_CMD_I2C_RESPONSE };
	struct i2c_response_rx rx_response;
	uint8_t size;
	uint8_t buffer[128];

	if (i2c_request->rx_length > sizeof(i2c_request->rx_data)) {
		ret = -EINVAL;
		goto out;
	}

	memset(&rx_response, 0, sizeof(rx_response));

	size = sizeof(rx_response) + i2c_request->rx_length + 1;

	ret = request(device, tx_response, sizeof(tx_response),
			buffer, size);

	if (ret != 0)
		goto out;

	memcpy(&rx_response, buffer, sizeof(rx_response));

	if (rx_response.status != 0) {
		ret = -EBUSY;
		goto out;
	}

	if (rx_response.result != 0) {
		ret = -EIO;
		goto out;
	}

	memcpy(i2c_request->rx_data, buffer + sizeof(rx_response),
			i2c_request->rx_length);
out:
	return ret;
}

int smartbattery_notify_action(
	struct smartbattery_device *device,
	struct smartbattery_action *action)
{
	int ret;
	struct notify_action_tx tx_data;
	struct notify_action_rx rx_data;

	memset(&tx_data, 0, sizeof(tx_data));
	memset(&rx_data, 0, sizeof(rx_data));

	tx_data.req_id = I2C_CMD_NOTIFY_ACTION;
	tx_data.type = action->type;
	tx_data.checksum = compute_checksum((uint8_t *)&tx_data,
		sizeof(tx_data)-1);

	ret = request(device, (uint8_t *)&tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));
	if (ret != 0)
		goto out;

	if (rx_data.result != 0) {
		ret = -EPERM;
		goto out;
	}

out:
	return ret;
}

static uint8_t get_soh(uint16_t full_charge_cap, uint16_t design_cap)
{
	uint8_t ret = 0;

	if (design_cap != 0)
		ret = (full_charge_cap * 100) / design_cap;

	if (ret > 100)
		ret = 100;

	return ret;
}

int smartbattery_get_alerts(
	struct smartbattery_device *device,
	struct smartbattery_alerts *alerts)
{
	int ret;
	uint8_t tx_data[] = { I2C_REQ_GET_ALERTS };
	struct get_alerts_rx rx_data;
	uint8_t val;

	ret = request(device, tx_data, sizeof(tx_data),
		(uint8_t *)&rx_data, sizeof(rx_data));

	if (ret != 0)
		goto out;

	alerts->fault = rx_data.fault;

	val = rx_data.value_gauge & 0x07;
	alerts->gauge = (enum smartbattery_alert_temperature)-1;
	switch (val) {
	case SMARTBATTERY_ALERT_TEMP_NONE:
	case SMARTBATTERY_ALERT_TEMP_HIGH_CRITICAL:
	case SMARTBATTERY_ALERT_TEMP_HIGH_WARNING:
	case SMARTBATTERY_ALERT_TEMP_LOW_CRITICAL:
	case SMARTBATTERY_ALERT_TEMP_LOW_WARNING:
		alerts->gauge = val;
		break;
	}

out:
	return ret;
}

int smartbattery_i2c_raw(
		struct smartbattery_device *device,
		struct smartbattery_i2c_raw *raw)
{
	int ret = -EINVAL;

	if (raw->tx_length > sizeof(raw->tx_data))
		goto out;
	if (raw->rx_length > sizeof(raw->rx_data))
		goto out;

	ret = request(device, raw->tx_data, raw->tx_length,
			raw->rx_data, raw->rx_length);

out:
	return ret;
}

int smartbattery_get_gauge(
	struct smartbattery_device *device,
	struct smartbattery_gauge *gauge)
{
	int ret;

	ret = smartbattery_get_voltage(device, &gauge->voltage);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_current(device, &gauge->current_val);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_remaining_cap(device, &gauge->remaining_cap);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_full_charge_cap(device, &gauge->full_charge_cap);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_design_cap(device, &gauge->design_cap);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_rsoc(device, &gauge->rsoc);
	if (ret != 0)
		goto out;

	gauge->soh.value = get_soh(gauge->full_charge_cap.value,
			gauge->design_cap.value);

	ret = smartbattery_get_temperature(device, &gauge->temperature);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_avg_time_to_empty(device,
			&gauge->avg_time_to_empty);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_avg_time_to_full(device,
			&gauge->avg_time_to_full);
	if (ret != 0)
		goto out;
	ret = smartbattery_get_avg_current(device, &gauge->avg_current);
	if (ret != 0)
		goto out;
out:
	return ret;
}

int smartbattery_get_charger(
	struct smartbattery_device *device,
	struct smartbattery_charger *charger)
{
	int ret;

	ret = smartbattery_get_max_charge_voltage(
			device,
			&charger->max_charge_voltage);
	if (ret != 0)
		goto out;

	ret = smartbattery_get_charging_current(
			device,
			&charger->charging_current);
	if (ret != 0)
		goto out;

	ret = smartbattery_get_input_voltage(
			device,
			&charger->input_voltage);
	if (ret != 0)
		goto out;

	ret = smartbattery_get_charger_temperature(device,
			&charger->temperature);
	if (ret != 0)
		goto out;
out:
	return ret;
}

int smartbattery_write_data(
		struct smartbattery_device *device,
		struct smartbattery_flash_area *area,
		const uint8_t *data)
{
	int ret;
	int rc;
	struct smartbattery_flash_chunk chunk;
	uint16_t address;
	uint16_t length;
	const uint8_t *p;
	uint16_t area_length = area->length;

	p = data;
	address = area->address;
	while (area_length > 0) {
		length = min((uint16_t)SMARTBATTERY_CHUNK_SIZE, area_length);
		chunk.address = address;
		chunk.length = length;
		memcpy(chunk.data, p, length);
		rc = smartbattery_write_flash(device, &chunk);
		if (rc < 0) {
			ret = rc;
			dev_err(&device->client->dev,
					"Write flash failed 0x%.4x / %u\n",
					address, length);
			goto out;
		}
		p += length;
		address += length;
		area_length -= length;
	}
	ret = 0;
out:
	return ret;
}

static const struct smartbattery_partition msp430g2533_partition[3] = {
	{
		.type           = SMARTBATTERY_PARTITION_APPLICATION,
		.begin          = 0xC000,
		.end            = 0xF5FF,
		.size           = 0xF600 - 0xC000,
		.segment_size   = 512,
		.version_offset = 0xF5FA,
		.crc_offset     = 0xF5FE,
		.name           = "application",
	}, {
		.type           = SMARTBATTERY_PARTITION_UPDATER,
		.begin          = 0xF600,
		.end            = 0xFDFF,
		.size           = 0xFE00 - 0xF600,
		.segment_size   = 512,
		.version_offset = 0xFDFA,
		.crc_offset     = 0xFDFE,
		.name           = "updater",
	}, {
		.type           = SMARTBATTERY_PARTITION_BOOTLOADER,
		.begin          = 0xFE00,
		.end            = 0xFFFF,
		.size           = 0x10000 - 0xFE00,
		.segment_size   = 512,
		.version_offset = 0xFFD8,
		.crc_offset     = 0xFFDC,
		.name           = "bootloader",
	},
};

static const struct smartbattery_flash_info msp430g2533_info = {
	.flash_begin = 0xC000,
	.flash_end = 0xFFFF,
	.flash_size = 16 * 1024,
};

int smartbattery_get_partition(
	struct smartbattery_ctrl_info *ctrl_info,
	struct smartbattery_partition *partition)
{
	int ret = -EINVAL;

	switch (partition->type) {
	case SMARTBATTERY_PARTITION_APPLICATION:
	case SMARTBATTERY_PARTITION_UPDATER:
	case SMARTBATTERY_PARTITION_BOOTLOADER:
		break;
	default:
		goto out;
	}

	switch (ctrl_info->type) {
	case SMARTBATTERY_CTRL_MSP430G2433:
		return -ENODEV;
	case SMARTBATTERY_CTRL_MSP430G2533:
		switch (ctrl_info->partitioning_type) {
		case 0:
			*partition = msp430g2533_partition[partition->type];
			ret = 0;
			break;
		}
		break;
	default:
		goto out;
	}
out:
	return ret;
}

int smartbattery_get_flash_info(
	enum smartbattery_ctrl_type ctrl_type,
	struct smartbattery_flash_info *flash_info)
{
	switch (ctrl_type) {
	case SMARTBATTERY_CTRL_MSP430G2433:
		return -ENODEV;
	case SMARTBATTERY_CTRL_MSP430G2533:
		*flash_info = msp430g2533_info;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int smartbattery_wait_active(
		struct smartbattery_device *device,
		struct smartbattery_state *state,
		int timeout_ms)
{
	int ret = -ETIMEDOUT;
	int step_ms = WAIT_ACTIVE_STEP_MS;
	int rc;

	while (timeout_ms > 0) {
		msleep(step_ms);
		rc = smartbattery_get_state(device, state);
		if (rc == 0) {
			ret = 0;
			goto out;
		}
		timeout_ms -= step_ms;
	}
out:

	return ret;
}

int smartbattery_start_app(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms)
{
	int ret;
	int rc;

	device->device_is_resetting = true;

	rc = reset_to(device, SMARTBATTERY_PARTITION_APPLICATION);
	if (rc < 0) {
		ret = rc;
		goto out;
	}
	if (state != NULL) {
		rc = smartbattery_wait_active(device, state, timeout_ms);
		if (rc < 0) {
			ret = rc;
			dev_err(&device->client->dev,
					"Wait active app has failed\n");
			goto out;
		}
		if (state->system_state != SMARTBATTERY_SYSTEM_READY) {
			ret = -EIO;
			dev_err(&device->client->dev, "Not Ready\n");
			goto out;
		}
	}
	ret = 0;
out:
	device->device_is_resetting = false;

	return ret;
}

int smartbattery_start_upd(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms)
{
	int ret;
	int rc;

	device->device_is_resetting = true;

	rc = reset_to(device, SMARTBATTERY_PARTITION_UPDATER);
	if (rc < 0) {
		ret = rc;
		goto out;
	}
	if (state != NULL) {
		rc = smartbattery_wait_active(device, state, timeout_ms);
		if (rc < 0) {
			ret = rc;
			dev_err(&device->client->dev,
					"Wait active upd has failed\n");
			goto out;
		}
		if (state->system_state != SMARTBATTERY_SYSTEM_UPDATER) {
			ret = -EIO;
			dev_err(&device->client->dev, "Not in Updater\n");
			goto out;
		}
	}
	ret = 0;
out:
	device->device_is_resetting = false;

	return ret;
}

int smartbattery_reset(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms)
{
	int ret;

	device->device_is_resetting = true;

	ret = reset_to(device, SMARTBATTERY_PARTITION_BOOTLOADER);
	if (ret < 0)
		goto out;
	if (state != NULL) {
		ret = smartbattery_wait_active(device, state, timeout_ms);
		if (ret < 0)
			goto out;
	}
	ret = 0;
out:
	device->device_is_resetting = false;

	return ret;
}
