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

#ifndef SMARTBATTERY_IOCTL_H
#define SMARTBATTERY_IOCTL_H

#include <linux/device.h>

#include "smartbattery.h"
#include "smartbattery_device.h"
#include "smartbattery_protocol.h"

#define SMARTBATTERY_IOCTL_NUM 'S'

#define SMARTBATTERY_GET_MAGIC          _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_MAGIC, struct smartbattery_magic)

#define SMARTBATTERY_GET_STATE          _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_STATE, struct smartbattery_state)

#define SMARTBATTERY_GET_VERSION        _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_VERSION, struct smartbattery_version)

#define SMARTBATTERY_GET_SERIAL         _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_SERIAL, struct smartbattery_serial)

#define SMARTBATTERY_GET_LOG            _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_LOG, struct smartbattery_log)

#define SMARTBATTERY_GET_CTRL_INFO      _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CTRL_INFO, struct smartbattery_ctrl_info)

#define SMARTBATTERY_GET_CHEMISTRY      _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CHEMISTRY, struct smartbattery_chemistry)

#define SMARTBATTERY_GET_MANUFACTURER   _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_MANUFACTURER, struct smartbattery_manufacturer)

#define SMARTBATTERY_GET_DEVICE_INFO    _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_DEVICE_INFO, struct smartbattery_device_info)

#define SMARTBATTERY_GET_HW_VERSION     _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_HW_VERSION, struct smartbattery_hw_version)

#define SMARTBATTERY_I2C_REQUEST        _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_I2C_REQUEST, struct smartbattery_i2c_request)

#define SMARTBATTERY_GET_CELL_CONFIG    _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CELL_CONFIG, struct smartbattery_cell_config)

#define SMARTBATTERY_GET_VOLTAGE        _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_VOLTAGE, struct smartbattery_voltage)

#define SMARTBATTERY_GET_CURRENT        _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CURRENT, struct smartbattery_current)

#define SMARTBATTERY_GET_REMAINING_CAP  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_REMAINING_CAP, struct smartbattery_capacity)

#define SMARTBATTERY_GET_FULL_CHARGE_CAP  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_FULL_CHARGE_CAP, struct smartbattery_capacity)

#define SMARTBATTERY_GET_DESIGN_CAP  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_DESIGN_CAP, struct smartbattery_capacity)

#define SMARTBATTERY_GET_RSOC  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_RSOC, struct smartbattery_rsoc)

#define SMARTBATTERY_GET_TEMPERATURE  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_TEMPERATURE, struct smartbattery_temperature)

#define SMARTBATTERY_GET_CHARGER_TEMPERATURE  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CHARGER_TEMPERATURE, struct smartbattery_temperature)

#define SMARTBATTERY_GET_MAX_CHARGE_VOLTAGE  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_MAX_CHARGE_VOLTAGE, struct smartbattery_voltage)

#define SMARTBATTERY_GET_CHARGING_CURRENT  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CHARGING_CURRENT, struct smartbattery_current)

#define SMARTBATTERY_GET_INPUT_VOLTAGE  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_INPUT_VOLTAGE, struct smartbattery_voltage)

#define SMARTBATTERY_GET_AVG_TIME_TO_EMPTY  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_AVG_TIME_TO_EMPTY, struct smartbattery_time)

#define SMARTBATTERY_GET_AVG_TIME_TO_FULL  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_AVG_TIME_TO_FULL, struct smartbattery_time)

#define SMARTBATTERY_GET_AVG_CURRENT  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_AVG_CURRENT, struct smartbattery_current)

#define SMARTBATTERY_CHECK_FLASH  _IOWR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_CHECK_FLASH, struct smartbattery_check_flash_area)

#define SMARTBATTERY_READ_FLASH  _IOWR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_READ_FLASH, struct smartbattery_flash_chunk)

#define SMARTBATTERY_I2C_RESPONSE  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_I2C_RESPONSE, struct smartbattery_i2c_request)

#define SMARTBATTERY_NOTIFY_ACTION      _IOWR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_NOTIFY_ACTION, struct smartbattery_action)

#define SMARTBATTERY_ERASE_FLASH  _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_ERASE_FLASH, struct smartbattery_flash_area)

#define SMARTBATTERY_WRITE_FLASH  _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_WRITE_FLASH, struct smartbattery_flash_chunk)

#define SMARTBATTERY_GET_MODE           _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_MODE, struct smartbattery_mode)

#define SMARTBATTERY_SET_MODE           _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_SET_MODE, struct smartbattery_mode)

#define SMARTBATTERY_SET_LEDS  _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_SET_LEDS, struct smartbattery_leds)

#define SMARTBATTERY_RESET_TO  _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_RESET_TO, struct smartbattery_reset_type)

#define SMARTBATTERY_GET_USB_PEER  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_USB_PEER, struct smartbattery_usb_peer)

#define SMARTBATTERY_GET_CELL_VOLTAGE  _IOWR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_CELL_VOLTAGE, struct smartbattery_cell_voltage)

#define SMARTBATTERY_WINTERING          _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_WINTERING, struct smartbattery_wintering)

#define SMARTBATTERY_SET_USB          _IOW(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_SET_USB, struct smartbattery_usb)

#define SMARTBATTERY_GET_ALERTS  _IOR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_REQ_GET_ALERTS, struct smartbattery_alerts)

#define SMARTBATTERY_I2C_RAW  _IOWR(SMARTBATTERY_IOCTL_NUM,    \
	I2C_CMD_I2C_RAW, struct smartbattery_i2c_raw)

int smartbattery_ioctl_init(struct smartbattery *sb);

int smartbattery_ioctl_exit(struct smartbattery *sb);

#endif /* SMARTBATTERY_IOCTL_H */
