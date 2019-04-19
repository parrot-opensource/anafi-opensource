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

#ifndef PARROT_SMARTBATTERY_H
#define PARROT_SMARTBATTERY_H

#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>

static const uint8_t SMARTBATTERY_MAGIC[] = {
	0x53, 0x50, 0x51, 0x52,
};

#define SMARTBATTERY_SERIAL_NUMBER_MAXLEN 18
#define SMARTBATTERY_MANUFACTURER_NAME_MAXLEN 8
#define SMARTBATTERY_DEVICE_NAME_MAXLEN 8
#define SMARTBATTERY_LED_MAX_SEQ 4
#define SMARTBATTERY_LED_MAX_COUNT 16
#define SMARTBATTERY_CHUNK_SIZE 26
#define SMARTBATTERY_I2C_BRIDGE_PAYLOAD_SIZE 18
#define SMARTBATTERY_MAX_CELL_COUNT 8

enum smartbattery_system_state {
	SMARTBATTERY_SYSTEM_UPDATER = 0,
	SMARTBATTERY_SYSTEM_READY = 1,
	SMARTBATTERY_SYSTEM_ERROR = 2,
};

enum smartbattery_system_mode {
	SMARTBATTERY_SYSTEM_NORMAL = 0,
	SMARTBATTERY_SYSTEM_LIGHT = 1,
	SMARTBATTERY_SYSTEM_MAINTENANCE = 2,
};

enum smartbattery_wintering_mode {
	SMARTBATTERY_WINTERING_ENTER = 0,
	SMARTBATTERY_WINTERING_AUTO_DISCHARGE = 1,
	SMARTBATTERY_WINTERING_SHUTDOWN = 2,
};

enum smartbattery_power_state {
	SMARTBATTERY_POWER_UNPLUGGED = 0,
	SMARTBATTERY_POWER_PLUGGED = 1,
};

enum smartbattery_drone_state {
	SMARTBATTERY_DRONE_NOT_DETECTED = 0,
	SMARTBATTERY_DRONE_DETECTED = 1,
	SMARTBATTERY_DRONE_ALIVE = 2,
};

enum smartbattery_battery_mode {
	SMARTBATTERY_DISCHARGING = 0,
	SMARTBATTERY_CHARGING = 1,
	SMARTBATTERY_FULLY_CHARGED = 2,
	SMARTBATTERY_FULLY_DISCHARGED = 3,
};

struct smartbattery_components {
	int ctrl_ok;
	int gauge_ok;
	int charger_ok;
	int power_ok;
};

enum smartbattery_component {
	SMARTBATTERY_USB = 0,
	SMARTBATTERY_CHARGER = 1,
	SMARTBATTERY_GAUGE = 2,
};

enum smartbattery_ctrl_type {
	SMARTBATTERY_CTRL_MSP430G2433 = 0, /* Flash: 8 KiB */
	SMARTBATTERY_CTRL_MSP430G2533 = 1, /* Flash: 16 KiB */
};

enum smartbattery_partition_type {
	SMARTBATTERY_PARTITION_APPLICATION = 0,
	SMARTBATTERY_PARTITION_UPDATER = 1,
	SMARTBATTERY_PARTITION_BOOTLOADER = 2,
};

enum smartbattery_chemistry_type {
	SMARTBATTERY_TYPE_LIPO = 0,
	SMARTBATTERY_TYPE_UNKNOWN = 0xff,
};

enum smartbattery_cell_code {
	SMARTBATTERY_CELL_CODE_2S1P = 0x21,
	SMARTBATTERY_CELL_CODE_UNKNOWN = 0xff,
};

enum smartbattery_led_mode {
	SMARTBATTERY_LED_MODE_MANUAL = 0,
	SMARTBATTERY_LED_MODE_AUTO = 1,
};

enum smartbattery_log_msg {
	SMARTBATTERY_LOG_EMPTY,
	SMARTBATTERY_LOG_STATE,
	SMARTBATTERY_LOG_TASK_GAUGE,
	SMARTBATTERY_LOG_TASK_GAUGE_DURATION,
	SMARTBATTERY_LOG_TASK_CHARGE,
	SMARTBATTERY_LOG_LEDS,
	SMARTBATTERY_LOG_ERROR,
	SMARTBATTERY_LOG_SUM_TASK,
	SMARTBATTERY_LOG_SUM_ISR,
	SMARTBATTERY_LOG_MAX_TASK,
	SMARTBATTERY_LOG_MAX_ISR,
	SMARTBATTERY_LOG_CHARGER_CURRENT_LIMIT,
	SMARTBATTERY_LOG_PROGRAM_CHARGER,
	SMARTBATTERY_LOG_DRONE_DETECTION,
	SMARTBATTERY_LOG_USB_HANDLER,
	SMARTBATTERY_LOG_BUTTON,
	SMARTBATTERY_LOG_INT_1_LOW,
	SMARTBATTERY_LOG_INT_1_HIGH,
	SMARTBATTERY_LOG_TYPE_C_LOW,
	SMARTBATTERY_LOG_TYPE_C_HIGH,
	SMARTBATTERY_LOG_MAX,
};

enum smartbattery_log_arg {
	/* for SMARTBATTERY_LOG_PROGRAM_CHARGER */
	SMARTBATTERY_LOG_CHARGER_PD = 0,
	SMARTBATTERY_LOG_CHARGER_SDP,
	SMARTBATTERY_LOG_CHARGER_CDP,
	SMARTBATTERY_LOG_CHARGER_DCP,
	SMARTBATTERY_LOG_CHARGER_LEGACY,
	SMARTBATTERY_LOG_CHARGER_ICO_OK,
	SMARTBATTERY_LOG_CHARGER_ICO_KO,
	SMARTBATTERY_LOG_CHARGER_OTG_ENABLE,
	SMARTBATTERY_LOG_CHARGER_OTG_DISABLE,
	/* for SMARTBATTERY_LOG_DRONE_DETECTION */
	SMARTBATTERY_LOG_DRONE_NOT_DETECTED = 0,
	SMARTBATTERY_LOG_DRONE_DETECTED,
	SMARTBATTERY_LOG_DRONE_ALIVE,
	/* for SMARTBATTERY_LOG_USB_HANDLER */
	SMARTBATTERY_LOG_USB_DETECT = 0,
	SMARTBATTERY_LOG_USB_READ_POWER,
	SMARTBATTERY_LOG_USB_READ_STATUS,
	SMARTBATTERY_LOG_USB_READ,
	SMARTBATTERY_LOG_USB_MANAGE,
	SMARTBATTERY_LOG_USB_PLUG,
	SMARTBATTERY_LOG_USB_UNPLUG,
	SMARTBATTERY_LOG_USB_PLUGGED,
	SMARTBATTERY_LOG_USB_UNPLUGGED,
	SMARTBATTERY_LOG_USB_SINK,
	SMARTBATTERY_LOG_USB_SOURCE,
	SMARTBATTERY_LOG_USB_HOST,
	SMARTBATTERY_LOG_USB_DEVICE,
	/* for SMARTBATTERY_LOG_BUTTON */
	SMARTBATTERY_LOG_BUTTON_PRESSED = 0,
	SMARTBATTERY_LOG_BUTTON_LONG_PRESSED,
	SMARTBATTERY_LOG_BUTTON_SHORT_RELEASED,
	SMARTBATTERY_LOG_BUTTON_LONG_RELEASED,
	/* for SMARTBATTERY_LOG_STATE */
	SMARTBATTERY_LOG_STATE_ACTIVE = 0,
	SMARTBATTERY_LOG_STATE_USB_DETECT,
	SMARTBATTERY_LOG_STATE_AWAKE,
	SMARTBATTERY_LOG_STATE_STANDBY,
	SMARTBATTERY_LOG_STATE_FAULT,
	SMARTBATTERY_LOG_STATE_AUTO_DISCHARGE,
};

enum smartbattery_usb_data_role {
	SMARTBATTERY_USB_ROLE_HOST = 0,
	SMARTBATTERY_USB_ROLE_DEVICE = 1,
};

enum smartbattery_usb_power_role {
	SMARTBATTERY_USB_ROLE_SINK = 0,
	SMARTBATTERY_USB_ROLE_SOURCE = 1,
};

enum smartbattery_usb_swap {
	SMARTBATTERY_USB_SWAP_TO_SINK = 0,
	SMARTBATTERY_USB_SWAP_TO_SOURCE = 1,
};

enum smartbattery_action_type {
	SMARTBATTERY_ACTION_START = 0,
	SMARTBATTERY_ACTION_POWEROFF = 1,
	SMARTBATTERY_ACTION_REBOOT = 2,
};

enum smartbattery_fault {
	SMARTBATTERY_FAULT_NONE = 0,
	SMARTBATTERY_FAULT_INFO_CRC,
	SMARTBATTERY_FAULT_GAUGE,
	SMARTBATTERY_FAULT_CHARGER,
	SMARTBATTERY_FAULT_USB,
	SMARTBATTERY_FAULT_SIGNATURES,
	SMARTBATTERY_FAULT_SPI_FLASH,
};

enum smartbattery_alert_temperature {
	SMARTBATTERY_ALERT_TEMP_NONE = 0,
	SMARTBATTERY_ALERT_TEMP_HIGH_CRITICAL,
	SMARTBATTERY_ALERT_TEMP_HIGH_WARNING,
	SMARTBATTERY_ALERT_TEMP_LOW_CRITICAL,
	SMARTBATTERY_ALERT_TEMP_LOW_WARNING,
};

struct smartbattery_magic {
	uint8_t value[4];
};

struct smartbattery_partition_version {
		uint8_t major;
		uint8_t minor;
		uint8_t patch;
		uint8_t variant;
};

struct smartbattery_version {
	struct smartbattery_partition_version application_version;
	struct smartbattery_partition_version updater_version;
	struct smartbattery_partition_version bootloader_version;
	uint16_t application_crc16;
	uint16_t updater_crc16;
	uint16_t bootloader_crc16;
};

struct smartbattery_state {
	enum smartbattery_system_state system_state;
	enum smartbattery_power_state power_state;
	enum smartbattery_drone_state drone_state;
	enum smartbattery_battery_mode battery_mode;
	struct smartbattery_components components;
};

struct smartbattery_mode {
	enum smartbattery_system_mode system_mode;
};

struct smartbattery_wintering {
	enum smartbattery_wintering_mode mode;
};

struct smartbattery_serial {
	char serial_number[SMARTBATTERY_SERIAL_NUMBER_MAXLEN+1];
	int is_empty;
};

struct smartbattery_chemistry {
	enum smartbattery_chemistry_type type;
	const char *type_name;
};

struct smartbattery_cell_config {
	enum smartbattery_cell_code code;
	const char *code_name;
};

struct smartbattery_manufacturer {
	char name[SMARTBATTERY_MANUFACTURER_NAME_MAXLEN+1];
	int is_empty;
};

struct smartbattery_device_info {
	char name[SMARTBATTERY_DEVICE_NAME_MAXLEN+1];
	int is_empty;
};

struct smartbattery_hw_version {
	uint8_t version;
};

struct smartbattery_reset_type {
	enum smartbattery_partition_type type;
};

struct smartbattery_ctrl_info {
	enum smartbattery_ctrl_type type;
	const char *type_name;
	uint8_t partition_count;
	uint8_t partitioning_type;
	uint8_t led_count;
};

struct smartbattery_partition {
	enum smartbattery_partition_type type; /* in */
	uint16_t begin; /* out */
	uint16_t end; /* out */
	uint16_t size; /* out */
	uint16_t segment_size; /* out */
	uint16_t version_offset; /* out */
	uint16_t crc_offset; /* out */
	const char *name; /* out */
};

struct smartbattery_flash_info {
	uint16_t flash_begin;
	uint16_t flash_end;
	uint16_t flash_size;
};

struct smartbattery_voltage {
	uint16_t value; /* mV */
};

struct smartbattery_cell_voltage {
	uint8_t index;
	uint16_t value; /* mV */
};

struct smartbattery_current {
	int16_t value; /* mA */
};

struct smartbattery_capacity {
	uint16_t value; /* mAh */
};

struct smartbattery_rsoc {
	uint16_t value; /* percentage */
};

struct smartbattery_soh {
	uint8_t value; /* percentage */
};

struct smartbattery_time {
	uint16_t value; /* min */
};

struct smartbattery_temperature {
	uint16_t value; /* 0.1 K */
};

struct smartbattery_gauge {
	struct smartbattery_voltage voltage;
	struct smartbattery_current current_val;
	struct smartbattery_capacity remaining_cap;
	struct smartbattery_capacity full_charge_cap;
	struct smartbattery_capacity design_cap;
	struct smartbattery_rsoc rsoc;
	struct smartbattery_soh soh;
	struct smartbattery_temperature temperature;
	struct smartbattery_time avg_time_to_empty;
	struct smartbattery_time avg_time_to_full;
	struct smartbattery_current avg_current;
};

struct smartbattery_charger {
	struct smartbattery_voltage max_charge_voltage;
	struct smartbattery_current charging_current;
	struct smartbattery_voltage input_voltage;
	struct smartbattery_temperature temperature;
};

struct smartbattery_leds {
	enum smartbattery_led_mode mode;
	bool sequence[SMARTBATTERY_LED_MAX_SEQ][SMARTBATTERY_LED_MAX_COUNT];
	uint8_t duration; /* unit = 100-ms */
};

struct smartbattery_log {
	uint32_t time_ms; /* ms */
	enum smartbattery_log_msg msg;
	uint16_t arg;
	int dropped;
};

struct smartbattery_flash_area {
	uint16_t address;
	uint16_t length;
};

struct smartbattery_check_flash_area {
	uint16_t address; /* in */
	uint16_t length; /* in */
	uint16_t crc16; /* out */
};

struct smartbattery_flash_chunk {
	uint16_t address;
	uint16_t length;
	uint8_t data[SMARTBATTERY_CHUNK_SIZE];
};

struct smartbattery_usb_peer {
	bool connected;
	enum smartbattery_usb_data_role data_role;
	enum smartbattery_usb_power_role power_role;
	void (*cb)(void *arg);
};

struct smartbattery_usb {
	enum smartbattery_usb_data_role data_role;
	enum smartbattery_usb_power_role power_role;
	enum smartbattery_usb_swap swap;
};

struct smartbattery_i2c_request {
	enum smartbattery_component component;
	uint8_t tx_length;
	uint8_t tx_data[SMARTBATTERY_I2C_BRIDGE_PAYLOAD_SIZE];
	uint8_t rx_length;
	uint8_t rx_data[SMARTBATTERY_I2C_BRIDGE_PAYLOAD_SIZE];
};

struct smartbattery_action {
	enum smartbattery_action_type type;
};

struct smartbattery_alerts {
	enum smartbattery_fault fault;
	enum smartbattery_alert_temperature gauge;
};

struct smartbattery_i2c_raw {
	uint8_t tx_data[32];
	size_t tx_length;
	uint8_t rx_data[32];
	size_t rx_length;
};

/* */
struct smartbattery_device {
	struct i2c_client *client;
	struct semaphore lock;
	bool device_is_resetting;
};

/* Application and Updater */

int smartbattery_get_magic(
	struct smartbattery_device *device,
	struct smartbattery_magic *magic);

int smartbattery_get_state(
	struct smartbattery_device *device,
	struct smartbattery_state *state);

int smartbattery_get_ctrl_info(
	struct smartbattery_device *device,
	struct smartbattery_ctrl_info *ctrl_info);

int smartbattery_get_partition(
	struct smartbattery_ctrl_info *ctrl_info,
	struct smartbattery_partition *partition);

int smartbattery_get_flash_info(
	enum smartbattery_ctrl_type ctrl_type,
	struct smartbattery_flash_info *flash_info);

int smartbattery_get_version(
	struct smartbattery_device *device,
	struct smartbattery_version *version);

int smartbattery_get_serial(
	struct smartbattery_device *device,
	struct smartbattery_serial *serial);

int smartbattery_get_chemistry(
	struct smartbattery_device *device,
	struct smartbattery_chemistry *chemistry);

int smartbattery_get_cell_config(
	struct smartbattery_device *device,
	struct smartbattery_cell_config *config);

int smartbattery_get_manufacturer(
	struct smartbattery_device *device,
	struct smartbattery_manufacturer *manufacturer);

int smartbattery_get_device_info(
	struct smartbattery_device *device,
	struct smartbattery_device_info *device_info);

int smartbattery_get_hw_version(
	struct smartbattery_device *device,
	struct smartbattery_hw_version *hw_version);

int smartbattery_reset_to(
	struct smartbattery_device *device,
	const struct smartbattery_reset_type *reset_type);

int smartbattery_reset(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms);

int smartbattery_wait_active(
		struct smartbattery_device *device,
		struct smartbattery_state *state,
		int timeout_ms);

int smartbattery_check_flash(
	struct smartbattery_device *device,
	struct smartbattery_check_flash_area *area);

int smartbattery_erase_flash(
	struct smartbattery_device *device,
	const struct smartbattery_flash_area *area);

int smartbattery_read_flash(
	struct smartbattery_device *device,
	struct smartbattery_flash_chunk *chunk);

int smartbattery_write_flash(
	struct smartbattery_device *device,
	const struct smartbattery_flash_chunk *chunk);

int smartbattery_write_data(
	struct smartbattery_device *device,
	struct smartbattery_flash_area *area,
	const uint8_t *data);

int smartbattery_read_data(
	struct smartbattery_device *device,
	struct smartbattery_flash_area *area,
	uint8_t *data);

/* Application only */

int smartbattery_get_log(
	struct smartbattery_device *device,
	struct smartbattery_log *log);

int smartbattery_get_voltage(
	struct smartbattery_device *device,
	struct smartbattery_voltage *voltage);

int smartbattery_get_cell_voltage(
	struct smartbattery_device *device,
	struct smartbattery_cell_voltage *voltage);

int smartbattery_get_current(
	struct smartbattery_device *device,
	struct smartbattery_current *current_val);

int smartbattery_get_remaining_cap(
	struct smartbattery_device *device,
	struct smartbattery_capacity *capacity);

int smartbattery_get_full_charge_cap(
	struct smartbattery_device *device,
	struct smartbattery_capacity *capacity);

int smartbattery_get_design_cap(
	struct smartbattery_device *device,
	struct smartbattery_capacity *capacity);

int smartbattery_get_rsoc(
	struct smartbattery_device *device,
	struct smartbattery_rsoc *rsoc);

int smartbattery_get_soh(
	struct smartbattery_device *device,
	struct smartbattery_soh *soh);

int smartbattery_get_temperature(
	struct smartbattery_device *device,
	struct smartbattery_temperature *temperature);

int smartbattery_get_charger_temperature(
	struct smartbattery_device *device,
	struct smartbattery_temperature *temperature);

int smartbattery_get_avg_time_to_empty(
	struct smartbattery_device *device,
	struct smartbattery_time *time);

int smartbattery_get_avg_time_to_full(
	struct smartbattery_device *device,
	struct smartbattery_time *time);

int smartbattery_get_avg_current(
	struct smartbattery_device *device,
	struct smartbattery_current *current_val);

int smartbattery_get_max_charge_voltage(
	struct smartbattery_device *device,
	struct smartbattery_voltage *max_charge_voltage);

int smartbattery_get_charging_current(
	struct smartbattery_device *device,
	struct smartbattery_current *charging_current);

int smartbattery_get_input_voltage(
	struct smartbattery_device *device,
	struct smartbattery_voltage *input_voltage);

int smartbattery_get_gauge(
	struct smartbattery_device *device,
	struct smartbattery_gauge *gauge);

int smartbattery_get_charger(
	struct smartbattery_device *device,
	struct smartbattery_charger *charger);

int smartbattery_set_leds(
	struct smartbattery_device *device,
	const struct smartbattery_leds *leds);

int smartbattery_set_leds_auto(struct smartbattery_device *device);

int smartbattery_set_leds_on(struct smartbattery_device *device);

int smartbattery_set_leds_off(struct smartbattery_device *device);

int smartbattery_set_leds_blink(struct smartbattery_device *device);

int smartbattery_get_usb_peer(
	struct smartbattery_device *device,
	struct smartbattery_usb_peer *peer);

int smartbattery_set_usb(
	struct smartbattery_device *device,
	const struct smartbattery_usb *usb);

int smartbattery_start_upd(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms);

int smartbattery_i2c_bridge_async(
	struct smartbattery_device *device,
	struct smartbattery_i2c_request *i2c_request);

int smartbattery_i2c_bridge_request(
	struct smartbattery_device *device,
	const struct smartbattery_i2c_request *i2c_request);

int smartbattery_i2c_bridge_response(
	struct smartbattery_device *device,
	struct smartbattery_i2c_request *i2c_request);

int smartbattery_i2c_read_u8(
	struct smartbattery_device *device,
	enum smartbattery_component component,
	uint8_t reg,
	uint8_t *value_p);

int smartbattery_i2c_read_u16(
	struct smartbattery_device *device,
	enum smartbattery_component component,
	uint8_t reg,
	uint16_t *value_p);

int smartbattery_i2c_read_u16_dual(
	struct smartbattery_device *device,
	enum smartbattery_component component,
	uint8_t reg,
	uint16_t *value_p);

int smartbattery_get_alerts(
	struct smartbattery_device *device,
	struct smartbattery_alerts *alerts);

int smartbattery_get_mode(
	struct smartbattery_device *device,
	struct smartbattery_mode *mode);

int smartbattery_set_mode(
	struct smartbattery_device *device,
	const struct smartbattery_mode *mode);

int smartbattery_wintering(
	struct smartbattery_device *device,
	const struct smartbattery_wintering *wintering);

int smartbattery_notify_action(
	struct smartbattery_device *device,
	struct smartbattery_action *action);

int smartbattery_i2c_raw(
	struct smartbattery_device *device,
	struct smartbattery_i2c_raw *raw);

/* Updater only */

int smartbattery_start_app(
	struct smartbattery_device *device,
	struct smartbattery_state *state,
	int timeout_ms);

#endif /* PARROT_SMARTBATTERY_H */
