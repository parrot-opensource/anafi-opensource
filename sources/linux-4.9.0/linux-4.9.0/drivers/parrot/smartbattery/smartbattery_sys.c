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

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/sysfs.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/device.h>

#include "smartbattery_common.h"
#include "smartbattery_crc16.h"
#include "smartbattery_device.h"
#include "smartbattery_sys.h"
#include "smartbattery_gauge.h"
#include "smartbattery.h"

#ifndef COUNT_OF
#define COUNT_OF(a)  (sizeof(a) / sizeof(*(a)))
#endif

static ssize_t show_present(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", sb->present);
}

static ssize_t show_magic(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	return sprintf(buf, "%.2x %.2x %.2x %.2x\n",
		sb->magic.value[0],
		sb->magic.value[1],
		sb->magic.value[2],
		sb->magic.value[3]);
}

static ssize_t show_sb_state(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d %d %d %d %d %d %d %d\n",
			sb->state.system_state,
			sb->state.power_state,
			sb->state.drone_state,
			sb->state.battery_mode,
			sb->state.components.ctrl_ok,
			sb->state.components.gauge_ok,
			sb->state.components.charger_ok,
			sb->state.components.power_ok);
}

static ssize_t show_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u.%u.%u.%u\n",
			sb->version.application_version.major,
			sb->version.application_version.minor,
			sb->version.application_version.patch,
			sb->version.application_version.variant);
}

static ssize_t show_full_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf,
			"%u.%u.%u.%u 0x%.4X "
			"%u.%u.%u.%u 0x%.4X "
			"%u.%u.%u.%u 0x%.4X\n",
			sb->version.application_version.major,
			sb->version.application_version.minor,
			sb->version.application_version.patch,
			sb->version.application_version.variant,
			sb->version.application_crc16,
			sb->version.updater_version.major,
			sb->version.updater_version.minor,
			sb->version.updater_version.patch,
			sb->version.updater_version.variant,
			sb->version.updater_crc16,
			sb->version.bootloader_version.major,
			sb->version.bootloader_version.minor,
			sb->version.bootloader_version.patch,
			sb->version.bootloader_version.variant,
			sb->version.bootloader_crc16);
}

static ssize_t show_ctrl_info(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d %d %d %d\n",
			sb->ctrl_info.type,
			sb->ctrl_info.partitioning_type,
			sb->ctrl_info.partition_count,
			sb->ctrl_info.led_count);
}

static ssize_t show_partition_0(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "0x%.4X 0x%.4X %u\n",
			 sb->partition_application.begin,
			 sb->partition_application.end,
			 sb->partition_application.segment_size);
}

static ssize_t show_partition_1(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "0x%.4X 0x%.4X %u\n",
			 sb->partition_updater.begin,
			 sb->partition_updater.end,
			 sb->partition_updater.segment_size);
}

static ssize_t show_partition_2(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "0x%.4X 0x%.4X %u\n",
			 sb->partition_bootloader.begin,
			 sb->partition_bootloader.end,
			 sb->partition_bootloader.segment_size);
}

static ssize_t show_serial(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (sb->serial.is_empty)
		return sprintf(buf, "none\n");
	return sprintf(buf, "%.*s\n",
			SMARTBATTERY_SERIAL_NUMBER_MAXLEN,
			sb->serial.serial_number);
}

static ssize_t show_chemistry(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (sb->chemistry.type == SMARTBATTERY_TYPE_LIPO)
		return sprintf(buf, "Li-Po\n");
	return sprintf(buf, "unknown\n");
}

static ssize_t show_cell_config(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (sb->cell_config.code == SMARTBATTERY_CELL_CODE_2S1P)
		return sprintf(buf, "2S1P\n");
	return sprintf(buf, "unknown\n");
}

static ssize_t show_manufacturer_name(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%.*s\n",
			SMARTBATTERY_MANUFACTURER_NAME_MAXLEN,
			sb->manufacturer.name);
}

static ssize_t show_model_name(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%s\n", sb->device_name);
}

static ssize_t show_device_info(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%.*s\n",
			SMARTBATTERY_DEVICE_NAME_MAXLEN,
			sb->device_info.name);
}

static ssize_t show_hw_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", sb->hw_version.version);
}

static ssize_t show_voltage(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	int value;

	if (!sb->present)
		return -ENODEV;

	if (sb->simu_enable)
		value = sb->simu_voltage;
	else
		value = sb->gauge.voltage.value;
	return sprintf(buf, "%u\n", value);
}

static ssize_t show_current(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d\n", sb->gauge.current_val.value);
}

static ssize_t show_remaining_cap(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.remaining_cap.value);
}

static ssize_t show_full_charge_cap(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.full_charge_cap.value);
}

static ssize_t show_design_cap(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.design_cap.value);
}

static ssize_t show_rsoc(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	int value;

	if (!sb->present)
		return -ENODEV;

	if (sb->simu_enable)
		value = sb->simu_rsoc;
	else
		value = sb->gauge.rsoc.value;
	return sprintf(buf, "%u\n", value);
}

static ssize_t show_soh(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.soh.value);
}

static ssize_t show_temperature(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.temperature.value);
}

static ssize_t show_charger_temperature(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->charger.temperature.value);
}

static ssize_t show_avg_time_to_empty(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.avg_time_to_empty.value);
}

static ssize_t show_avg_time_to_full(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->gauge.avg_time_to_full.value);
}

static ssize_t show_avg_current(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d\n", sb->gauge.avg_current.value);
}

static ssize_t show_gauge(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u %d %u %u %u %u %u %u %u %d\n",
			sb->gauge.voltage.value,
			sb->gauge.current_val.value,
			sb->gauge.remaining_cap.value,
			sb->gauge.full_charge_cap.value,
			sb->gauge.design_cap.value,
			sb->gauge.rsoc.value,
			sb->gauge.temperature.value,
			sb->gauge.avg_time_to_empty.value,
			sb->gauge.avg_time_to_full.value,
			sb->gauge.avg_current.value);
}

static ssize_t show_max_charge_voltage(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->charger.max_charge_voltage.value);
}

static ssize_t show_charging_current(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d\n", sb->charger.charging_current.value);
}

static ssize_t show_input_voltage(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u\n", sb->charger.input_voltage.value);
}

static ssize_t show_usb_peer(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%d %d %d\n",
		sb->usb_peer.connected,
		sb->usb_peer.data_role,
		sb->usb_peer.power_role);
}

static ssize_t show_usb_peer_status(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%sconnected\n",
		sb->usb_peer.connected ? "" : "dis");
}

static ssize_t show_usb_peer_power(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (!sb->usb_peer.connected)
		return sprintf(buf, "disconnected\n");
	return sprintf(buf, "%s\n",
		sb->usb_peer.power_role == SMARTBATTERY_USB_ROLE_SOURCE ?
			"source" :
		sb->usb_peer.power_role == SMARTBATTERY_USB_ROLE_SINK ?
			"sink" : "unknown");
}

static ssize_t show_usb_peer_data(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (!sb->usb_peer.connected)
		return sprintf(buf, "disconnected\n");
	return sprintf(buf, "%s\n",
		sb->usb_peer.data_role == SMARTBATTERY_USB_ROLE_HOST ?
			"host" :
		sb->usb_peer.data_role == SMARTBATTERY_USB_ROLE_DEVICE ?
			"device" : "unknown");

}

static ssize_t show_charger(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u %d %u %u\n",
			sb->charger.max_charge_voltage.value,
			sb->charger.charging_current.value,
			sb->charger.input_voltage.value,
			sb->charger.temperature.value);
}

static ssize_t store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	struct smartbattery_state state;
	int rc;

	if (!sb->present)
		return -ENODEV;

	rc = smartbattery_reset(&sb->device, &state, RESET_TIMEOUT_MS);
	if (rc != 0)
		return -EIO;

	return count;
}

static ssize_t store_notify_action(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	struct smartbattery_action action;
	int rc;

	if (!sb->present)
		return -ENODEV;

	if (!strncmp(buf, "start", sizeof("start")-1))
		action.type = SMARTBATTERY_ACTION_START;
	else if (!strncmp(buf, "poweroff", sizeof("poweroff")-1))
		action.type = SMARTBATTERY_ACTION_POWEROFF;
	else if (!strncmp(buf, "reboot", sizeof("reboot")-1))
		action.type = SMARTBATTERY_ACTION_REBOOT;
	else
		return -EINVAL;

	rc = smartbattery_notify_action(&sb->device, &action);
	if (rc != 0)
		return -EIO;

	return count;
}

static void init_leds(
		struct smartbattery_leds *leds,
		enum smartbattery_led_mode mode,
		bool value,
		uint8_t duration)
{
	int i;
	int j;

	leds->mode = mode;
	for (j = 0; j < SMARTBATTERY_LED_MAX_SEQ; j++) {
		for (i = 0; i < SMARTBATTERY_LED_MAX_COUNT; i++)
			leds->sequence[j][i] = value;
	}
	leds->duration = duration;
}

static ssize_t store_leds(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	struct smartbattery_leds leds;
	int array[8];
	int nb;
	int rc = -1;
	int i;
	int j;

	if (!sb->present)
		return -ENODEV;

	if (!strncmp(buf, "auto", sizeof("auto")-1))
		rc = smartbattery_set_leds_auto(&sb->device);
	else {
		nb = sscanf(buf, "%d %d %d %d %d %d %d %d",
			&array[0],
			&array[1],
			&array[2],
			&array[3],
			&array[4],
			&array[5],
			&array[6],
			&array[7]);
		if (nb != 8)
			return -EINVAL;
		init_leds(&leds, SMARTBATTERY_LED_MODE_MANUAL, false, 0);
		for (j = 0; j < SMARTBATTERY_LED_MAX_SEQ; j++)
			for (i = 0; i < 8; i++)
				leds.sequence[j][i] = (array[i] == 1);
		rc = smartbattery_set_leds(&sb->device, &leds);
	}

	if (rc != 0)
		return -EIO;

	return count;
}

static ssize_t show_usb_customer_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	int rc;
	struct smartbattery_i2c_request i2c_request;

	if (!sb->present)
		return -ENODEV;

	i2c_request.component = SMARTBATTERY_USB;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 3;
	i2c_request.tx_data[0] = 6;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		return -EIO;

	return sprintf(buf, "%u.%u\n",
			i2c_request.rx_data[1],
			i2c_request.rx_data[2]);
}

static ssize_t show_gauge_fw_version(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x\n",
			sb->gauge_fw_version[0],
			sb->gauge_fw_version[1],
			sb->gauge_fw_version[2],
			sb->gauge_fw_version[3],
			sb->gauge_fw_version[4],
			sb->gauge_fw_version[5],
			sb->gauge_fw_version[6],
			sb->gauge_fw_version[7],
			sb->gauge_fw_version[8],
			sb->gauge_fw_version[9]);
}

static ssize_t show_gauge_signatures(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%.4x%.4x\n",
			sb->gauge_signatures[0],
			sb->gauge_signatures[1]);
}

static ssize_t show_gauge_date(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	uint8_t day;
	uint8_t month;
	uint16_t year;

	if (!sb->present)
		return -ENODEV;

	smartbattery_gauge_value2date(sb->gauge_date, &day, &month, &year);

	return sprintf(buf, "%.2d/%.2d/%.4d\n", day, month, year);
}

static ssize_t show_alerts(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%u %u\n",
			sb->alerts.fault,
			sb->alerts.gauge);
}

static const char * const fault_str[] = {
	[SMARTBATTERY_FAULT_NONE]               = "none",
	[SMARTBATTERY_FAULT_INFO_CRC]           = "info-crc",
	[SMARTBATTERY_FAULT_GAUGE]              = "gauge",
	[SMARTBATTERY_FAULT_CHARGER]            = "charger",
	[SMARTBATTERY_FAULT_USB]                = "usb",
	[SMARTBATTERY_FAULT_SIGNATURES]         = "signatures",
	[SMARTBATTERY_FAULT_SPI_FLASH]          = "spi-flash",
};

static ssize_t show_fault(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	const char *str = "Unknown";

	if (!sb->present)
		return -ENODEV;

	if (sb->alerts.fault < COUNT_OF(fault_str))
		str = fault_str[sb->alerts.fault];

	return sprintf(buf, "%s\n", str);
}

static const char * const alert_str[] = {
	[SMARTBATTERY_ALERT_TEMP_NONE]          = "none",
	[SMARTBATTERY_ALERT_TEMP_HIGH_CRITICAL] = "high-critical",
	[SMARTBATTERY_ALERT_TEMP_HIGH_WARNING]  = "high-warning",
	[SMARTBATTERY_ALERT_TEMP_LOW_CRITICAL]  = "low-critical",
	[SMARTBATTERY_ALERT_TEMP_LOW_WARNING]   = "low-warning",
};

static ssize_t show_gauge_alert(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	const char *str = "Unknown";

	if (!sb->present)
		return -ENODEV;

	if (sb->alerts.gauge < COUNT_OF(alert_str))
		str = alert_str[sb->alerts.gauge];

	return sprintf(buf, "%s\n", str);
}

static DEVICE_ATTR(present, S_IRUSR, show_present, NULL);
static DEVICE_ATTR(magic, S_IRUSR, show_magic, NULL);
static DEVICE_ATTR(state, S_IRUSR, show_sb_state, NULL);
static DEVICE_ATTR(version, S_IRUSR, show_version, NULL);
static DEVICE_ATTR(full_version, S_IRUSR, show_full_version, NULL);
static DEVICE_ATTR(ctrl_info, S_IRUSR, show_ctrl_info, NULL);
static DEVICE_ATTR(partition_0, S_IRUSR, show_partition_0, NULL);
static DEVICE_ATTR(partition_1, S_IRUSR, show_partition_1, NULL);
static DEVICE_ATTR(partition_2, S_IRUSR, show_partition_2, NULL);
static DEVICE_ATTR(serial, S_IRUSR, show_serial, NULL);
static DEVICE_ATTR(chemistry, S_IRUSR, show_chemistry, NULL);
static DEVICE_ATTR(cell_config, S_IRUSR, show_cell_config, NULL);
static DEVICE_ATTR(manufacturer_name, S_IRUSR, show_manufacturer_name, NULL);
static DEVICE_ATTR(model_name, S_IRUSR, show_model_name, NULL);
static DEVICE_ATTR(device_info, S_IRUSR, show_device_info, NULL);
static DEVICE_ATTR(hw_version, S_IRUSR, show_hw_version, NULL);
static DEVICE_ATTR(voltage_now, S_IRUSR, show_voltage, NULL);
static DEVICE_ATTR(current_now, S_IRUSR, show_current, NULL);
static DEVICE_ATTR(remaining_cap, S_IRUSR, show_remaining_cap, NULL);
static DEVICE_ATTR(full_charge_cap, S_IRUSR, show_full_charge_cap, NULL);
static DEVICE_ATTR(design_cap, S_IRUSR, show_design_cap, NULL);
static DEVICE_ATTR(rsoc, S_IRUSR, show_rsoc, NULL);
static DEVICE_ATTR(soh, S_IRUSR, show_soh, NULL);
static DEVICE_ATTR(temperature, S_IRUSR, show_temperature, NULL);
static DEVICE_ATTR(charger_temperature, S_IRUSR,
		show_charger_temperature, NULL);
static DEVICE_ATTR(avg_time_to_empty, S_IRUSR, show_avg_time_to_empty, NULL);
static DEVICE_ATTR(avg_time_to_full, S_IRUSR, show_avg_time_to_full, NULL);
static DEVICE_ATTR(avg_current, S_IRUSR, show_avg_current, NULL);
static DEVICE_ATTR(gauge, S_IRUSR, show_gauge, NULL);
static DEVICE_ATTR(max_charge_voltage, S_IRUSR, show_max_charge_voltage, NULL);
static DEVICE_ATTR(charging_current, S_IRUSR, show_charging_current, NULL);
static DEVICE_ATTR(input_voltage, S_IRUSR, show_input_voltage, NULL);
static DEVICE_ATTR(charger, S_IRUSR, show_charger, NULL);
static DEVICE_ATTR(reset, S_IWUSR, NULL, store_reset);
static DEVICE_ATTR(usb_peer, S_IRUSR, show_usb_peer, NULL);
static DEVICE_ATTR(usb_peer_status, S_IRUSR, show_usb_peer_status, NULL);
static DEVICE_ATTR(usb_peer_power, S_IRUSR, show_usb_peer_power, NULL);
static DEVICE_ATTR(usb_peer_data, S_IRUSR, show_usb_peer_data, NULL);
static DEVICE_ATTR(notify_action, S_IWUSR, NULL, store_notify_action);
static DEVICE_ATTR(leds, S_IWUSR, NULL, store_leds);
static DEVICE_ATTR(usb_customer_version, S_IRUSR, show_usb_customer_version,
		NULL);
static DEVICE_ATTR(gauge_fw_version, S_IRUSR, show_gauge_fw_version, NULL);
static DEVICE_ATTR(gauge_signatures, S_IRUSR, show_gauge_signatures, NULL);
static DEVICE_ATTR(gauge_date, S_IRUSR, show_gauge_date, NULL);
static DEVICE_ATTR(alerts, S_IRUSR, show_alerts, NULL);
static DEVICE_ATTR(fault, S_IRUSR, show_fault, NULL);
static DEVICE_ATTR(gauge_alert, S_IRUSR, show_gauge_alert, NULL);

static struct attribute *smartbattery_attrs[] = {
	&dev_attr_present.attr,
	&dev_attr_magic.attr,
	&dev_attr_state.attr,
	&dev_attr_version.attr,
	&dev_attr_full_version.attr,
	&dev_attr_ctrl_info.attr,
	&dev_attr_partition_0.attr,
	&dev_attr_partition_1.attr,
	&dev_attr_partition_2.attr,
	&dev_attr_serial.attr,
	&dev_attr_chemistry.attr,
	&dev_attr_cell_config.attr,
	&dev_attr_manufacturer_name.attr,
	&dev_attr_model_name.attr,
	&dev_attr_device_info.attr,
	&dev_attr_hw_version.attr,
	&dev_attr_voltage_now.attr,
	&dev_attr_current_now.attr,
	&dev_attr_remaining_cap.attr,
	&dev_attr_full_charge_cap.attr,
	&dev_attr_design_cap.attr,
	&dev_attr_rsoc.attr,
	&dev_attr_soh.attr,
	&dev_attr_temperature.attr,
	&dev_attr_charger_temperature.attr,
	&dev_attr_avg_time_to_empty.attr,
	&dev_attr_avg_time_to_full.attr,
	&dev_attr_avg_current.attr,
	&dev_attr_gauge.attr,
	&dev_attr_max_charge_voltage.attr,
	&dev_attr_charging_current.attr,
	&dev_attr_input_voltage.attr,
	&dev_attr_charger.attr,
	&dev_attr_reset.attr,
	&dev_attr_usb_peer.attr,
	&dev_attr_usb_peer_status.attr,
	&dev_attr_usb_peer_power.attr,
	&dev_attr_usb_peer_data.attr,
	&dev_attr_notify_action.attr,
	&dev_attr_leds.attr,
	&dev_attr_usb_customer_version.attr,
	&dev_attr_gauge_fw_version.attr,
	&dev_attr_gauge_signatures.attr,
	&dev_attr_gauge_date.attr,
	&dev_attr_alerts.attr,
	&dev_attr_fault.attr,
	&dev_attr_gauge_alert.attr,
	NULL,
};

static struct attribute_group smartbattery_attr_group = {
	.attrs = smartbattery_attrs,
};

static void usb_peer_cb(void *arg)
{
	struct smartbattery_device *device = (struct smartbattery_device *)arg;

	if (!device )
		return;

	sysfs_notify(&device->client->dev.kobj, NULL, "usb_peer_data");
}

int smartbattery_create_sysfs_cb(struct smartbattery *sb)
{
	/* Callback for usb_peer_data */
	sb->usb_peer.cb = usb_peer_cb;

	return 0;
}

void smartbattery_delete_sysfs_cb(struct smartbattery *sb)
{
	sb->usb_peer.cb = NULL;
}

int smartbattery_create_sysfs_entries(struct smartbattery *sb)
{
	return sysfs_create_group(
			&sb->device.client->dev.kobj,
			&smartbattery_attr_group);
}

void smartbattery_delete_sysfs_entries(struct smartbattery *sb)
{
	sysfs_remove_group(
			&sb->device.client->dev.kobj,
			&smartbattery_attr_group);
}
