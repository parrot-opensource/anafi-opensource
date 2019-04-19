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

#ifndef SMARTBATTERY_DEVICE_H
#define SMARTBATTERY_DEVICE_H

#include <linux/power_supply.h>
#include <linux/miscdevice.h>
#include <linux/notifier.h>
#include <linux/kthread.h>
#include <linux/sched/prio.h>

#include "smartbattery.h"

/* timeout to let Smartbattery starts after reset */
#define RESET_TIMEOUT_MS 2500

#if defined(CONFIG_PARROT_SMARTBATTERY_KTHREAD_PRIO)
#define SMARTBATTERY_TASK_PRIORITY CONFIG_PARROT_SMARTBATTERY_KTHREAD_PRIO
#else
#define SMARTBATTERY_TASK_PRIORITY 1
#endif

struct smartbattery {
	struct smartbattery_device device;
	char *firmware_name;
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct power_supply_config bat_config;
	int present;
	/* */
	struct kthread_worker *worker;
	struct kthread_delayed_work dwork;
	/* */
	struct miscdevice misc_dev;
	/* */
	struct smartbattery_magic magic;
	struct smartbattery_state state;
	struct smartbattery_version version;
	struct smartbattery_serial serial;
	struct smartbattery_ctrl_info ctrl_info;
	struct smartbattery_partition partition_application;
	struct smartbattery_partition partition_updater;
	struct smartbattery_partition partition_bootloader;
	struct smartbattery_gauge gauge;
	struct smartbattery_charger charger;
	struct smartbattery_manufacturer manufacturer;
	struct smartbattery_device_info device_info;
	struct smartbattery_hw_version hw_version;
	struct smartbattery_chemistry chemistry;
	struct smartbattery_cell_config cell_config;
	struct smartbattery_usb_peer usb_peer;
	struct smartbattery_alerts alerts;
	char device_name[SMARTBATTERY_DEVICE_NAME_MAXLEN+1+3+1];
	bool application_valid;
	bool updater_valid;
	/* */
	bool is_authenticated;
	/* */
	bool simu_enable;
	int simu_rsoc;
	int simu_voltage;
	/* */
	uint8_t gauge_fw_version[12];
	uint16_t gauge_signatures[2];
	uint16_t gauge_date;
	/* */
	struct notifier_block restart_handler;
	struct notifier_block reboot_handler;
};

#endif /* SMARTBATTERY_DEVICE_H */
