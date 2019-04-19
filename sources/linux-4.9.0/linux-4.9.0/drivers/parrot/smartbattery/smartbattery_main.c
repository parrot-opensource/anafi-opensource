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
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/kernel.h>

#include "smartbattery_auth.h"
#include "smartbattery_common.h"
#include "smartbattery_misc.h"
#include "smartbattery_crc16.h"
#include "smartbattery_device.h"
#include "smartbattery_sys.h"
#include "smartbattery_simu.h"
#include "smartbattery_ioctl.h"
#include "smartbattery_property.h"
#include "smartbattery_reboot.h"
#include "smartbattery_update.h"
#include "smartbattery.h"

#define SMARTBATTERY_DRV_NAME "smartbattery"

static unsigned int update_time = 1000;
module_param(update_time, uint, 0644);
MODULE_PARM_DESC(update_time, "update time in milliseconds");

static bool allow_downgrade;
module_param(allow_downgrade, bool, false);
MODULE_PARM_DESC(allow_downgrade, "allow downgrade");

static char * firmware_name = NULL;
module_param(firmware_name, charp, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(firmware_name, "Firmware name");

static int kthread_prio = SMARTBATTERY_TASK_PRIORITY;
module_param(kthread_prio, int, 0644);
MODULE_PARM_DESC(kthread_prio, "kthread priority");

/* percentage */
#define MINIMUM_RSOC                    5

static void check_state(struct smartbattery *sb)
{
	static bool ctrl_ok = true;
	static bool gauge_ok = true;
	static bool charger_ok = true;
	static bool power_ok = true;

	if (!sb->state.components.ctrl_ok) {
		if (ctrl_ok) {
			dev_err(&sb->device.client->dev,
					"Controller problem detected\n");
			ctrl_ok = false;
		}
	} else
		ctrl_ok = true;

	if (!sb->state.components.gauge_ok) {
		if (gauge_ok) {
			dev_warn(&sb->device.client->dev,
					"Gauge problem detected\n");
			gauge_ok = false;
		}
	} else
		gauge_ok = true;

	if (!sb->state.components.charger_ok) {
		if (charger_ok) {
			dev_warn(&sb->device.client->dev,
					"Charger problem detected\n");
			charger_ok = false;
		}
	} else
		charger_ok = true;

	if (!sb->state.components.power_ok) {
		if (power_ok) {
			dev_warn(&sb->device.client->dev,
					"USB problem detected\n");
			power_ok = false;
		}
	} else
		power_ok = true;
}

/* no mercy for un-authenticated devices */
static void smartbattery_process_unauthenticated(
		struct smartbattery *sb,
		struct smartbattery_gauge *gauge)
{
	gauge->rsoc.value = 2;
	gauge->remaining_cap.value = 30;
	gauge->voltage.value = 6900;
}

static int acquisition(struct smartbattery *sb)
{
	int ret = -ENODEV;
	int rc;
	struct smartbattery_gauge gauge;

	dev_dbg(&sb->device.client->dev, "acquisition()\n");

	if (!sb->present)
		goto out;

	ret = -EIO;

	gauge = sb->gauge;

	rc = smartbattery_get_state(&sb->device, &sb->state);
	if (rc != 0)
		goto out;

	/* acquisition cannot be done in Updater */
	if (sb->state.system_state == SMARTBATTERY_SYSTEM_UPDATER)
		goto out;

	check_state(sb);

	if (sb->state.components.gauge_ok) {
		rc = smartbattery_get_gauge(&sb->device, &gauge);
		if (rc < 0)
			dev_err(&sb->device.client->dev,
					"Gauge Acquisition Error\n");
	}

	if (sb->state.components.charger_ok) {
		rc = smartbattery_get_charger(&sb->device, &sb->charger);
		if (rc < 0)
			dev_err(&sb->device.client->dev,
					"Charger Acquisition Error\n");
	}

	rc = smartbattery_get_usb_peer(&sb->device, &sb->usb_peer);
	if (rc < 0)
		dev_err(&sb->device.client->dev, "USB Peer Error\n");

	rc = smartbattery_get_alerts(&sb->device, &sb->alerts);
	if (rc < 0)
		dev_err(&sb->device.client->dev, "Alerts error\n");

	if (!smartbattery_auth_check(sb))
		sb->is_authenticated = false;

	if (!sb->is_authenticated) {
		static bool first_log_display = true;
		if (first_log_display)
			dev_warn(
				&sb->device.client->dev,
				"Unauthenticated Smartbattery detected\n");
		smartbattery_process_unauthenticated(sb, &gauge);
		first_log_display = false;
	}

	sb->gauge = gauge;

	ret = 0;
out:
	return ret;
}

static void smartbattery_work(struct kthread_work *work)
{
	struct smartbattery *sb = container_of(
			work,
			struct smartbattery,
			dwork.work);

	dev_dbg(&sb->device.client->dev, "smartbattery_work()\n");

	acquisition(sb);

	kthread_queue_delayed_work(
			sb->worker,
			&sb->dwork,
			msecs_to_jiffies(update_time));
}

static ssize_t show_update_time(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n", update_time);
}

static ssize_t show_allow_downgrade(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%s\n", allow_downgrade ? "true" : "false");
}

static ssize_t show_firmware_name(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", sb->firmware_name);
}

static ssize_t show_kthread_prio(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n", kthread_prio);
}

static ssize_t store_authenticate(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	bool res;

	if (!sb->present)
		return -ENODEV;

	if (!strncmp(buf, "ko", sizeof("ko")-1)) {
		sb->is_authenticated = false;
		return count;
	}

	res = smartbattery_auth_check_full(sb);
	if (!res) {
		sb->is_authenticated = false;
		return -EIO;
	}

	return count;
}

static DEVICE_ATTR(update_time, S_IRUSR, show_update_time, NULL);
static DEVICE_ATTR(allow_downgrade, S_IRUSR, show_allow_downgrade, NULL);
static DEVICE_ATTR(firmware_name, S_IRUSR, show_firmware_name, NULL);
static DEVICE_ATTR(kthread_prio, S_IRUSR, show_kthread_prio, NULL);
static DEVICE_ATTR(authenticate, S_IWUSR, NULL, store_authenticate);

static struct attribute *smartbattery_attrs[] = {
	&dev_attr_update_time.attr,
	&dev_attr_allow_downgrade.attr,
	&dev_attr_firmware_name.attr,
	&dev_attr_kthread_prio.attr,
	&dev_attr_authenticate.attr,
	NULL,
};

static struct attribute_group smartbattery_attr_group = {
	.attrs = smartbattery_attrs,
};

static int create_sysfs(struct smartbattery *sb)
{
	return sysfs_create_group(
			&sb->device.client->dev.kobj,
			&smartbattery_attr_group);
}

static void delete_sysfs(struct smartbattery *sb)
{
	sysfs_remove_group(
			&sb->device.client->dev.kobj,
			&smartbattery_attr_group);
}

static int smartbattery_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = -EIO;
	struct device_node *node = client->dev.of_node;
	const char *fw_name;
	int rc;
	struct smartbattery *sb = NULL;
	bool to_update;
	const struct sched_param param = {
		.sched_priority = kthread_prio,
	};

	if (!node)
		return -ENODEV;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_I2C))
		return -ENOSYS;

	rc = of_property_read_string(node, "firmware-name", &fw_name);
	if (rc) {
		dev_err(&client->dev,
				"failed to parse firmware-name property\n");
		return -EINVAL;
	}
	if (firmware_name != NULL)
		fw_name = firmware_name;
	dev_info(&client->dev, "Firmware Name=%s\n", fw_name);

	sb = kzalloc(sizeof(*sb), GFP_KERNEL);
	if (sb == NULL)
		return -ENOMEM;

	sb->firmware_name = kstrdup(fw_name, GFP_KERNEL);
	if (sb->firmware_name == NULL) {
		kfree(sb);
		return -ENOMEM;
	}

	i2c_set_clientdata(client, sb);

	sb->device.client = client;

	sema_init(&sb->device.lock, 1);

	/* by default, we trust the Smartbattery */
	sb->is_authenticated = true;

	smartbattery_read_info(sb);

	/* Update Firmware only if RSOC is enough or we boot in Updater */
	to_update = ((sb->gauge.rsoc.value >= MINIMUM_RSOC) |
		     (sb->state.system_state == SMARTBATTERY_SYSTEM_UPDATER));
	if (sb->present) {
		if (to_update) {
			rc = smartbattery_upload_firmware(
					sb, fw_name, allow_downgrade);
			if (rc < 0)
				dev_err(&sb->device.client->dev,
						"Cannot Upload Firmware %s\n",
						fw_name);
		} else {
			dev_warn(&sb->device.client->dev,
					"RSOC is too low (%d %%)\n",
					sb->gauge.rsoc.value);
		}

		/* expected state is Ready */
		if (sb->state.system_state != SMARTBATTERY_SYSTEM_READY) {
			dev_info(&sb->device.client->dev,
				"Expected state is \"Ready\"\n");
			rc = smartbattery_start_app(&sb->device, &sb->state,
					RESET_TIMEOUT_MS);
			if (rc != 0)
				dev_err(&sb->device.client->dev,
						"Cannot start application\n");
			smartbattery_read_info(sb);
		}

		rc = smartbattery_set_leds_auto(&sb->device);
		if (rc < 0)
			dev_err(&sb->device.client->dev, "Cannot set LEDs\n");
	}

	acquisition(sb);

	/* */
	sb->worker = kthread_create_worker(0, "smartbattery");
	if (IS_ERR(sb->worker)) {
		dev_err(&client->dev, "Cannot create worker\n");
		goto out;
	}
	kthread_init_delayed_work(&sb->dwork, smartbattery_work);
	sched_setscheduler(sb->worker->task, SCHED_FIFO, &param);
	dev_info(&client->dev, "kthread priority=%d\n", kthread_prio);

	/* */
	memset(&sb->bat_desc, 0, sizeof(sb->bat_desc));
	memset(&sb->bat_config, 0, sizeof(sb->bat_config));
	sb->bat_desc.name = "smartbattery";
	sb->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	smartbattery_property_fill(&sb->bat_desc);
	sb->bat_config.drv_data = sb;
	sb->bat = power_supply_register(
			&client->dev,
			&sb->bat_desc,
			&sb->bat_config);
	if (sb->bat == NULL)
		goto out;

	rc = smartbattery_register_reboot(sb);
	if (rc)
		goto out;

	rc = smartbattery_create_sysfs_cb(sb);
	if (rc)
		goto out;

	rc = smartbattery_create_sysfs_entries(sb);
	if (rc)
		goto out;

	rc = create_sysfs(sb);
	if (rc)
		goto out;

	rc = smartbattery_simu_init(sb);
	if (rc)
		goto out;

	rc = smartbattery_ioctl_init(sb);
	if (rc)
		goto out;

	kthread_queue_delayed_work(
			sb->worker,
			&sb->dwork,
			msecs_to_jiffies(update_time));

	ret = 0;
out:
	if (ret < 0) {
		kfree(sb->firmware_name);
		kfree(sb);
	}
	return ret;
}

static int smartbattery_i2c_remove(struct i2c_client *client)
{
	struct smartbattery *sb = i2c_get_clientdata(client);

	kthread_cancel_delayed_work_sync(&sb->dwork);
	kthread_destroy_worker(sb->worker);

	smartbattery_ioctl_exit(sb);

	smartbattery_simu_exit(sb);

	delete_sysfs(sb);

	smartbattery_delete_sysfs_entries(sb);

	smartbattery_delete_sysfs_cb(sb);

	if (sb->bat != NULL)
		power_supply_unregister(sb->bat);

	smartbattery_unregister_reboot(sb);

	kfree(sb->firmware_name);
	kfree(sb);

	return 0;
}

static const struct i2c_device_id smartbattery_i2c_id[] = {
	{"smartbattery", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, smartbattery_i2c_id);

static const struct of_device_id smartbattery_i2c_of_match[] = {
	{ .compatible = "parrot,smartbattery", },
	{}
};
MODULE_DEVICE_TABLE(of, smartbattery_i2c_of_match);

static struct i2c_driver smartbattery_driver = {
	.probe		=	smartbattery_i2c_probe,
	.remove		=	smartbattery_i2c_remove,
	.id_table       =       smartbattery_i2c_id,
	.driver = {
		.owner	=	THIS_MODULE,
		.name	=	SMARTBATTERY_DRV_NAME,
		.of_match_table	= of_match_ptr(smartbattery_i2c_of_match),
	},
};

module_i2c_driver(smartbattery_driver);

MODULE_AUTHOR("Thierry Baldo <thierry.baldo@parrot.com>");
MODULE_DESCRIPTION("Parrot Smartbattery Driver");
MODULE_LICENSE("GPL");
