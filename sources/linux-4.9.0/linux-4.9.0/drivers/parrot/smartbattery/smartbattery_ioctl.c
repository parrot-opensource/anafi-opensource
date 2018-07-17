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

#include <linux/uaccess.h>

#include "smartbattery_ioctl.h"

static struct smartbattery *g_smartbattery;

#define FUNCTION(a) smartbattery_##a

#define SMARTBATTERY_READ(name, variable)				\
	rc = FUNCTION(name)(&sb->device, &variable);			\
	if (rc < 0)							\
		return -EIO;						\
	if (copy_to_user((void __user *)arg, &variable,			\
			   sizeof(variable)))				\
		return -EFAULT;						\
	ret = 0;

#define SMARTBATTERY_WRITE(name, variable)				\
	if (copy_from_user(&variable, (const void __user *)arg,		\
				sizeof(variable)))			\
		return -EFAULT;						\
	rc = FUNCTION(name)(&sb->device, &variable);			\
	if (rc < 0)							\
		return -EIO;						\
	ret = 0;

#define SMARTBATTERY_WRITE_READ(name, variable)				\
	if (copy_from_user(&variable, (const void __user *)arg,		\
				sizeof(variable)))			\
		return -EFAULT;						\
	rc = FUNCTION(name)(&sb->device, &variable);			\
	if (rc < 0)							\
		return -EIO;						\
	if (copy_to_user((void __user *)arg,				\
				&variable, sizeof(variable)))		\
		return -EFAULT;						\
	ret = 0;

static int sb_open(struct inode *inode, struct file *filp)
{
	filp->private_data = g_smartbattery;

	return 0;
}

static int sb_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static long sb_ioctl(struct file *filp, unsigned int req,
		       unsigned long arg)
{
	long ret = -EINVAL;
	struct smartbattery *sb;
	struct smartbattery_log log;
	struct smartbattery_check_flash_area check_area;
	struct smartbattery_flash_chunk chunk;
	struct smartbattery_flash_area area;
	struct smartbattery_leds leds;
	struct smartbattery_reset_type reset_type;
	struct smartbattery_i2c_request i2c_request;
	struct smartbattery_cell_voltage cell_voltage;
	struct smartbattery_mode mode;
	struct smartbattery_wintering wintering;
	struct smartbattery_usb usb;
	struct smartbattery_action action;
	struct smartbattery_alerts alerts;
	struct smartbattery_i2c_raw raw;
	int rc;

	sb = filp->private_data;

	switch (req) {
	case SMARTBATTERY_GET_MAGIC:
		SMARTBATTERY_READ(get_magic, sb->magic);
		break;
	case SMARTBATTERY_GET_STATE:
		SMARTBATTERY_READ(get_state, sb->state);
		break;
	case SMARTBATTERY_GET_VERSION:
		SMARTBATTERY_READ(get_version, sb->version);
		break;
	case SMARTBATTERY_GET_SERIAL:
		SMARTBATTERY_READ(get_serial, sb->serial);
		break;
	case SMARTBATTERY_GET_LOG:
		SMARTBATTERY_READ(get_log, log);
		break;
	case SMARTBATTERY_GET_CTRL_INFO:
		SMARTBATTERY_READ(get_ctrl_info, sb->ctrl_info);
		break;
	case SMARTBATTERY_GET_CHEMISTRY:
		SMARTBATTERY_READ(get_chemistry, sb->chemistry);
		break;
	case SMARTBATTERY_GET_MANUFACTURER:
		SMARTBATTERY_READ(get_manufacturer, sb->manufacturer);
		break;
	case SMARTBATTERY_GET_DEVICE_INFO:
		SMARTBATTERY_READ(get_device_info, sb->device_info);
		break;
	case SMARTBATTERY_GET_HW_VERSION:
		SMARTBATTERY_READ(get_hw_version, sb->hw_version);
		break;
	case SMARTBATTERY_I2C_REQUEST:
		SMARTBATTERY_WRITE(i2c_bridge_request, i2c_request);
		break;
	case SMARTBATTERY_GET_CELL_CONFIG:
		SMARTBATTERY_READ(get_cell_config, sb->cell_config);
		break;
	case SMARTBATTERY_GET_VOLTAGE:
		SMARTBATTERY_READ(get_voltage, sb->gauge.voltage);
		break;
	case SMARTBATTERY_GET_CELL_VOLTAGE:
		SMARTBATTERY_WRITE_READ(get_cell_voltage, cell_voltage);
		break;
	case SMARTBATTERY_GET_CURRENT:
		SMARTBATTERY_READ(get_current, sb->gauge.current_val);
		break;
	case SMARTBATTERY_GET_REMAINING_CAP:
		SMARTBATTERY_READ(get_remaining_cap, sb->gauge.remaining_cap);
		break;
	case SMARTBATTERY_GET_FULL_CHARGE_CAP:
		SMARTBATTERY_READ(get_full_charge_cap,
				sb->gauge.full_charge_cap);
		break;
	case SMARTBATTERY_GET_DESIGN_CAP:
		SMARTBATTERY_READ(get_design_cap, sb->gauge.design_cap);
		break;
	case SMARTBATTERY_GET_RSOC:
		SMARTBATTERY_READ(get_rsoc, sb->gauge.rsoc);
		break;
	case SMARTBATTERY_GET_TEMPERATURE:
		SMARTBATTERY_READ(get_temperature, sb->gauge.temperature);
		break;
	case SMARTBATTERY_GET_CHARGER_TEMPERATURE:
		SMARTBATTERY_READ(get_charger_temperature,
				sb->charger.temperature);
		break;
	case SMARTBATTERY_GET_MAX_CHARGE_VOLTAGE:
		SMARTBATTERY_READ(get_max_charge_voltage,
				sb->charger.max_charge_voltage);
		break;
	case SMARTBATTERY_GET_CHARGING_CURRENT:
		SMARTBATTERY_READ(get_charging_current,
				sb->charger.charging_current);
		break;
	case SMARTBATTERY_GET_INPUT_VOLTAGE:
		SMARTBATTERY_READ(get_input_voltage,
				sb->charger.input_voltage);
		break;
	case SMARTBATTERY_GET_AVG_TIME_TO_EMPTY:
		SMARTBATTERY_READ(get_avg_time_to_empty,
				sb->gauge.avg_time_to_empty);
		break;
	case SMARTBATTERY_GET_AVG_TIME_TO_FULL:
		SMARTBATTERY_READ(get_avg_time_to_full,
				sb->gauge.avg_time_to_full);
		break;
	case SMARTBATTERY_GET_AVG_CURRENT:
		SMARTBATTERY_READ(get_avg_current,
				sb->gauge.avg_current);
		break;
	case SMARTBATTERY_CHECK_FLASH:
		SMARTBATTERY_WRITE_READ(check_flash, check_area);
		break;
	case SMARTBATTERY_READ_FLASH:
		SMARTBATTERY_WRITE_READ(read_flash, chunk);
		break;
	case SMARTBATTERY_I2C_RESPONSE:
		SMARTBATTERY_WRITE_READ(i2c_bridge_response, i2c_request);
		break;
	case SMARTBATTERY_NOTIFY_ACTION:
		SMARTBATTERY_WRITE_READ(notify_action, action);
		break;
	case SMARTBATTERY_ERASE_FLASH:
		SMARTBATTERY_WRITE(erase_flash, area);
		break;
	case SMARTBATTERY_WRITE_FLASH:
		SMARTBATTERY_WRITE(write_flash, chunk);
		break;
	case SMARTBATTERY_GET_MODE:
		SMARTBATTERY_READ(get_mode, mode);
		break;
	case SMARTBATTERY_SET_MODE:
		SMARTBATTERY_WRITE(set_mode, mode);
		break;
	case SMARTBATTERY_SET_LEDS:
		SMARTBATTERY_WRITE(set_leds, leds);
		break;
	case SMARTBATTERY_RESET_TO:
		SMARTBATTERY_WRITE(reset_to, reset_type);
		break;
	case SMARTBATTERY_GET_USB_PEER:
		SMARTBATTERY_READ(get_usb_peer, sb->usb_peer);
		break;
	case SMARTBATTERY_WINTERING:
		SMARTBATTERY_WRITE(wintering, wintering);
		break;
	case SMARTBATTERY_SET_USB:
		SMARTBATTERY_WRITE(set_usb, usb);
		break;
	case SMARTBATTERY_GET_ALERTS:
		SMARTBATTERY_READ(get_alerts, alerts);
		break;
	case SMARTBATTERY_I2C_RAW:
		SMARTBATTERY_WRITE_READ(i2c_raw, raw);
		break;
	}

	return ret;
}

static const struct file_operations sb_fops = {
	.owner		= THIS_MODULE,
	.open		= sb_open,
	.release	= sb_release,
	.unlocked_ioctl	= sb_ioctl,
};

int smartbattery_ioctl_init(struct smartbattery *sb)
{
	int ret = -EIO;
	int rc;

	sb->misc_dev.minor = MISC_DYNAMIC_MINOR;
	sb->misc_dev.name = "smartbattery";
	sb->misc_dev.fops = &sb_fops;

	rc = misc_register(&sb->misc_dev);
	if (rc < 0)
		goto out;

	sb->dev = sb->misc_dev.this_device;

	g_smartbattery = sb;

	ret = 0;
out:
	return ret;
}

int smartbattery_ioctl_exit(struct smartbattery *sb)
{
	misc_deregister(&sb->misc_dev);

	g_smartbattery = NULL;

	return 0;
}
