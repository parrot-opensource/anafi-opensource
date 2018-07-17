/**
 * Copyright (c) 2018 Parrot Drones SAS
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
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/pm.h>

#include "smartbattery_common.h"
#include "smartbattery_device.h"
#include "smartbattery_misc.h"
#include "smartbattery_reboot.h"
#include "smartbattery.h"

static struct smartbattery *smartbattery_sb;
static int reboot_status = -ENODEV;

static int smartbattery_reboot(struct notifier_block *this,
                unsigned long mode, void *cmd)
{
	struct smartbattery_action action;
	struct smartbattery *sb = smartbattery_sb;
	if (mode == SYS_RESTART) {
		action.type = SMARTBATTERY_ACTION_REBOOT;
		reboot_status = smartbattery_notify_action(
				&sb->device,
				&action);
		dev_info(&sb->device.client->dev,
				"smartbattery_restart status %d\n",
				reboot_status);
	}

	return NOTIFY_DONE;
}

static int smartbattery_restart(struct notifier_block *this,
                unsigned long mode, void *cmd)
{
	if (reboot_status == 0)
		pm_power_off();

	return NOTIFY_DONE;
}

int smartbattery_register_reboot(struct smartbattery *sb)
{
	int rc;
	if (!sb->present)
		return 0;
	if (!pm_power_off)
		return 0;

	smartbattery_sb = sb;

	sb->reboot_handler.priority = 192;
	sb->reboot_handler.notifier_call = smartbattery_reboot;
	rc = register_reboot_notifier(&sb->reboot_handler);
	if (rc != 0)
		return rc;

	sb->restart_handler.priority = 192;
	sb->restart_handler.notifier_call = smartbattery_restart;
	rc = register_restart_handler(&sb->restart_handler);
	if (rc != 0) {
		unregister_reboot_notifier(&sb->reboot_handler);
		return rc;
	}

	dev_info(&sb->device.client->dev, "using smartbattery reboot\n");
	return rc;
}

void smartbattery_unregister_reboot(struct smartbattery *sb)
{
	unregister_restart_handler(&sb->restart_handler);
	unregister_reboot_notifier(&sb->reboot_handler);
}
