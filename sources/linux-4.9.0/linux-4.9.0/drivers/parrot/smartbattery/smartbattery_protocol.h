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

#ifndef SMARTBATTERY_PROTOCOL_H
#define SMARTBATTERY_PROTOCOL_H

#include "smartbattery.h"

/* for read_flash and write_flash */
#define SMARTBATTERY_FLASH_BUFFER_SIZE 26

enum i2c_cmd {
	I2C_REQ_GET_MAGIC = 0,
	I2C_REQ_GET_STATE = 1,
	I2C_REQ_GET_VERSION = 2,
	I2C_REQ_GET_SERIAL = 3,
	I2C_REQ_GET_LOG = 4,
	I2C_REQ_GET_CTRL_INFO = 5,
	I2C_REQ_GET_CHEMISTRY = 6,
	I2C_REQ_GET_MANUFACTURER = 7,
	I2C_REQ_GET_DEVICE_INFO = 8,
	I2C_REQ_GET_HW_VERSION = 9,
	I2C_CMD_I2C_REQUEST = 10,
	I2C_REQ_GET_CELL_CONFIG = 11,
	I2C_REQ_GET_VOLTAGE = 12,
	I2C_REQ_GET_CURRENT = 13,
	I2C_REQ_GET_REMAINING_CAP = 14,
	I2C_REQ_GET_FULL_CHARGE_CAP = 15,
	I2C_REQ_GET_DESIGN_CAP = 16,
	I2C_REQ_GET_RSOC = 17,
	I2C_REQ_GET_TEMPERATURE = 18,
	I2C_REQ_GET_MAX_CHARGE_VOLTAGE = 19,
	I2C_REQ_GET_CHARGING_CURRENT = 20,
	I2C_REQ_GET_AVG_TIME_TO_EMPTY = 21,
	I2C_REQ_GET_AVG_TIME_TO_FULL = 22,
	I2C_REQ_GET_AVG_CURRENT = 23,
	I2C_REQ_CHECK_FLASH = 30,
	I2C_REQ_READ_FLASH = 31,
	I2C_CMD_I2C_RESPONSE = 32,
	I2C_CMD_ERASE_FLASH = 33,
	I2C_CMD_WRITE_FLASH = 34,
	I2C_REQ_GET_MODE = 35,
	I2C_CMD_SET_MODE = 36,
	I2C_CMD_NOTIFY_ACTION = 37,
	I2C_CMD_SET_LEDS = 38,
	I2C_CMD_RESET_TO = 39,
	I2C_REQ_GET_USB_PEER = 40,
	I2C_REQ_GET_CELL_VOLTAGE = 41,
	I2C_CMD_WINTERING = 42,
	I2C_REQ_GET_INPUT_VOLTAGE = 43,
	I2C_CMD_SET_USB = 44,
	I2C_REQ_GET_ALERTS = 45,
	I2C_REQ_GET_CHARGER_TEMPERATURE = 46,
	I2C_CMD_I2C_RAW = 47,
	I2C_CMD_MAX,
};

struct __attribute__((__packed__)) get_magic_rx {
	uint8_t req_id;
	uint8_t value[4];
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_serial_rx {
	uint8_t req_id;
	uint8_t serial[18];
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_version_rx {
	uint8_t req_id;
	uint32_t application_version;
	uint32_t updater_version;
	uint32_t bootloader_version;
	uint16_t application_crc16;
	uint16_t updater_crc16;
	uint16_t bootloader_crc16;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_state_rx {
	uint8_t req_id;
	uint16_t state;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_chemistry_rx {
	uint8_t req_id;
	uint8_t type;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_cell_config_rx {
	uint8_t req_id;
	uint8_t code;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_manufacturer_rx {
	uint8_t req_id;
	uint8_t name[SMARTBATTERY_MANUFACTURER_NAME_MAXLEN];
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_device_info_rx {
	uint8_t req_id;
	uint8_t name[SMARTBATTERY_DEVICE_NAME_MAXLEN];
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_hw_version_rx {
	uint8_t req_id;
	uint8_t version;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_ctrl_info_rx {
	uint8_t req_id;
	uint8_t type;
	uint8_t partitioning_type;
	uint8_t partition_count;
	uint8_t led_count;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_voltage_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_cell_voltage_tx {
	uint8_t req_id;
	uint8_t index;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_cell_voltage_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_current_rx {
	uint8_t req_id;
	int16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_remaining_cap_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_full_charge_cap_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_design_cap_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_rsoc_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_temperature_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_avg_time_to_empty_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_avg_time_to_full_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_avg_current_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_max_charge_voltage_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_charging_current_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_input_voltage_rx {
	uint8_t req_id;
	uint16_t value;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_log_rx {
	uint8_t req_id;
	/* time_ms: 22 bits, reserved: 1 bit, dropped: 1 bit, msg: 8 bits */
	uint32_t header;
	uint16_t arg;
	uint8_t checksum;
};

struct __attribute__((__packed__)) reset_to_tx {
	uint8_t req_id;
	uint8_t partition_type;
	uint8_t checksum;
};

struct __attribute__((__packed__)) reset_to_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_usb_peer_rx {
	uint8_t req_id;
	uint8_t connected;
	uint8_t data_role;
	uint8_t power_role;
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_usb_tx {
	uint8_t req_id;
	uint8_t power_role;
	uint8_t data_role;
	uint8_t swap;
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_usb_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) check_flash_tx {
	uint8_t req_id;
	uint16_t address;
	uint16_t length;
	uint8_t checksum;
};

struct __attribute__((__packed__)) check_flash_rx {
	uint8_t req_id;
	uint16_t crc16;
	uint8_t checksum;
};

struct __attribute__((__packed__)) read_flash_tx {
	uint8_t req_id;
	uint16_t address;
	uint16_t length;
	uint8_t checksum;
};

struct __attribute__((__packed__)) read_flash_rx {
	uint8_t req_id;
	uint16_t address;
	uint16_t length;
	uint8_t data[SMARTBATTERY_FLASH_BUFFER_SIZE];
	uint8_t checksum;
};

struct __attribute__((__packed__)) write_flash_tx {
	uint8_t req_id;
	uint16_t address;
	uint16_t length;
	uint8_t data[SMARTBATTERY_FLASH_BUFFER_SIZE];
	uint8_t checksum;
};

struct __attribute__((__packed__)) write_flash_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) erase_flash_tx {
	uint8_t req_id;
	uint16_t address;
	uint16_t length;
	uint8_t checksum;
};

struct __attribute__((__packed__)) erase_flash_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_leds_tx {
	uint8_t req_id;
	uint8_t mode;
	/* only for manual mode
	 * 16-bits = on/off
	 */
	uint16_t sequence[SMARTBATTERY_LED_MAX_SEQ];
	uint8_t duration; /* 100-ms */
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_leds_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_mode_rx {
	uint8_t req_id;
	uint8_t system_mode;
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_mode_tx {
	uint8_t req_id;
	uint8_t system_mode;
	uint8_t checksum;
};

struct __attribute__((__packed__)) set_mode_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) wintering_tx {
	uint8_t req_id;
	uint8_t mode;
	uint8_t checksum;
};

struct __attribute__((__packed__)) wintering_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) i2c_request_tx {
	uint8_t req_id;
	uint8_t size;
	uint8_t component_id;
	uint8_t tx_len;
	uint8_t rx_len;
	/* tx data then checksum follow */
};

struct __attribute__((__packed__)) i2c_request_rx {
	uint8_t req_id;
	uint8_t checksum;
};

struct __attribute__((__packed__)) i2c_response_rx {
	uint8_t req_id;
	uint8_t status;
	int8_t result;
	/* rx data then checksum follow */
};

struct __attribute__((__packed__)) notify_action_tx {
	uint8_t req_id;
	uint8_t type;
	uint8_t checksum;
};

struct __attribute__((__packed__)) notify_action_rx {
	uint8_t req_id;
	int8_t result;
	uint8_t checksum;
};

struct __attribute__((__packed__)) get_alerts_rx {
	uint8_t req_id;
	uint8_t fault;
	uint8_t value_gauge:3;
	uint8_t checksum;
};

#endif /* SMARTBATTERY_PROTOCOL_H */
