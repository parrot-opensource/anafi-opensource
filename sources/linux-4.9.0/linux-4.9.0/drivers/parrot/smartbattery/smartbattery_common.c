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
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include "smartbattery_common.h"
#include "smartbattery_misc.h"
#include "smartbattery_gauge.h"

int smartbattery_check_partition(
		struct smartbattery_device *device,
		const char *label,
		struct smartbattery_partition *partition,
		uint16_t expected_crc16,
		bool *is_valid)
{
	int ret = -1;
	int rc;
	struct smartbattery_flash_chunk chunk;
	uint16_t crc16;

	chunk.address = partition->crc_offset;
	chunk.length = sizeof(crc16);
	rc = smartbattery_read_flash(device, &chunk);
	if (rc != 0) {
		dev_err(&device->client->dev,
				"Cannot read flash 0x%.4x / %u\n",
				chunk.address,
				chunk.length);
		goto out;
	}
	crc16 = le16toh(*(uint16_t*)chunk.data);
	dev_info(&device->client->dev, "CRC: read=0x%.4x computed=0x%.4x\n",
		crc16,
		expected_crc16);
	*is_valid = (crc16 == expected_crc16);
	if (!*is_valid)
		dev_warn(&device->client->dev,
				"%s is not valid\n", label);
	ret = 0;

out:
	if (ret != 0)
		dev_err(&device->client->dev,
				"Cannot check partition %s\n", label);
	return ret;
}

int smartbattery_read_info(struct smartbattery *sb)
{
	int ret = -EIO;
	int rc;
	int count = 10;
	struct smartbattery_mode mode;

	sb->present = 0;

	while (count--) {
		rc = smartbattery_get_magic(&sb->device, &sb->magic);
		if (rc == 0)
			break;
		msleep(100);
	}

	rc = smartbattery_get_magic(&sb->device, &sb->magic);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Smartbattery not detected\n");
		goto out;
	} else
		dev_info(&sb->device.client->dev, "Smartbattery detected\n");
	sb->present = 1;
	if (memcmp(sb->magic.value, SMARTBATTERY_MAGIC,
                sizeof(SMARTBATTERY_MAGIC))) {
		dev_err(&sb->device.client->dev,
				"Bad Magic (%.2x %.2x %.2x %.2x)\n",
				sb->magic.value[0],
				sb->magic.value[1],
				sb->magic.value[2],
				sb->magic.value[3]);
		goto out;
	}

	rc = smartbattery_get_state(&sb->device, &sb->state);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get State\n");
		goto out;
	}

	if (sb->state.system_state == SMARTBATTERY_SYSTEM_UPDATER)
		dev_info(&sb->device.client->dev,
				"Smartbattery in Updater mode\n");
	else if (sb->state.system_state == SMARTBATTERY_SYSTEM_READY)
		dev_info(&sb->device.client->dev, "Smartbattery is ready\n");
	else
		dev_info(&sb->device.client->dev,
				"Smartbattery state unknown\n");

	rc = smartbattery_get_ctrl_info(&sb->device, &sb->ctrl_info);
	if (rc != 0) {
		dev_err(&sb->device.client->dev,
				"Cannot get Controller Info\n");
		goto out;
	}

	sb->partition_application.type = SMARTBATTERY_PARTITION_APPLICATION;
	rc = smartbattery_get_partition(&sb->ctrl_info,
			&sb->partition_application);
	if (rc != 0) {
		dev_err(&sb->device.client->dev,
				"Cannot get partition Application\n");
		goto out;
	}
	sb->partition_updater.type = SMARTBATTERY_PARTITION_UPDATER;
	rc = smartbattery_get_partition(&sb->ctrl_info, &sb->partition_updater);
	if (rc != 0) {
		dev_err(&sb->device.client->dev,
				"Cannot get partition Updater\n");
		goto out;
	}
	sb->partition_bootloader.type = SMARTBATTERY_PARTITION_BOOTLOADER;
	rc = smartbattery_get_partition(&sb->ctrl_info,
			&sb->partition_bootloader);
	if (rc != 0) {
		dev_err(&sb->device.client->dev,
				"Cannot get partition Bootloader\n");
		goto out;
	}

	rc = smartbattery_get_manufacturer(&sb->device, &sb->manufacturer);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get Manufacturer\n");
		goto out;
	}

	rc = smartbattery_get_device_info(&sb->device, &sb->device_info);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get Device Info\n");
		goto out;
	}

	rc = smartbattery_get_hw_version(&sb->device, &sb->hw_version);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get HW Version\n");
		goto out;
	}

	snprintf(sb->device_name, sizeof(sb->device_name), "%s-%d",
			sb->device_info.name,
			sb->hw_version.version);

	rc = smartbattery_get_version(&sb->device, &sb->version);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get Version\n");
		goto out;
	}
	dev_info(&sb->device.client->dev,
			"Application: %u.%u.%u.%u (CRC=0x%x)\n",
			sb->version.application_version.major,
			sb->version.application_version.minor,
			sb->version.application_version.patch,
			sb->version.application_version.variant,
			sb->version.application_crc16);
	dev_info(&sb->device.client->dev,
			"Updater: %u.%u.%u.%u (CRC=0x%x)\n",
			sb->version.updater_version.major,
			sb->version.updater_version.minor,
			sb->version.updater_version.patch,
			sb->version.updater_version.variant,
			sb->version.updater_crc16);
	dev_info(&sb->device.client->dev,
			"Bootloader: %u.%u.%u.%u (CRC=0x%x)\n",
			sb->version.bootloader_version.major,
			sb->version.bootloader_version.minor,
			sb->version.bootloader_version.patch,
			sb->version.bootloader_version.variant,
			sb->version.bootloader_crc16);

	rc = smartbattery_check_partition(
			&sb->device,
			"Application",
			&sb->partition_application,
			sb->version.application_crc16,
			&sb->application_valid);
	if (rc < 0)
		goto out;

	rc = smartbattery_check_partition(
			&sb->device,
			"Updater",
			&sb->partition_updater,
			sb->version.updater_crc16,
			&sb->updater_valid);
	if (rc < 0)
		goto out;

	/* get misc values */
	rc = smartbattery_get_serial(&sb->device, &sb->serial);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Cannot get Serial\n");
		goto out;
	}
	if (sb->serial.is_empty)
		dev_info(&sb->device.client->dev, "No Serial Number\n");
	else
		dev_info(&sb->device.client->dev, "Serial Number: %s\n",
				sb->serial.serial_number);

	rc = smartbattery_get_chemistry(&sb->device, &sb->chemistry);
	if (rc != 0)
		dev_warn(&sb->device.client->dev, "Cannot get Chemistry\n");

	rc = smartbattery_get_cell_config(&sb->device, &sb->cell_config);
	if (rc != 0)
		dev_warn(&sb->device.client->dev, "Cannot get Cell Config\n");

	rc = smartbattery_get_gauge(&sb->device, &sb->gauge);
	if (rc != 0)
		dev_warn(&sb->device.client->dev, "Cannot get Gauge\n");

	rc = smartbattery_get_charger(&sb->device, &sb->charger);
	if (rc != 0)
		dev_warn(&sb->device.client->dev, "Cannot get Charger\n");

	mode.system_mode = SMARTBATTERY_SYSTEM_MAINTENANCE;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc != 0)
		dev_warn(&sb->device.client->dev,
				"Cannot go to Maintenance\n");

	rc = smartbattery_get_gauge_fw_version(sb);
	if (rc != 0)
		dev_warn(&sb->device.client->dev,
				"Cannot get Gauge FW Version\n");

	rc = smartbattery_get_gauge_signatures(sb);
	if (rc != 0)
		dev_warn(&sb->device.client->dev,
				"Cannot get Gauge Signatures\n");

	rc = smartbattery_get_gauge_date(sb);
	if (rc != 0)
		dev_warn(&sb->device.client->dev,
				"Cannot get Gauge Date\n");

	mode.system_mode = SMARTBATTERY_SYSTEM_NORMAL;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc != 0)
		dev_warn(&sb->device.client->dev,
				"Cannot go to Maintenance\n");

	ret = 0;
out:
	return ret;
}
