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
#include <linux/firmware.h>

#include "smartbattery_common.h"
#include "smartbattery_device.h"
#include "smartbattery_misc.h"
#include "smartbattery_crc16.h"
#include "smartbattery_update.h"
#include "smartbattery.h"

static int go_to_updater(struct smartbattery *sb)
{
	int ret = -1;
	int rc;

	dev_dbg(&sb->device.client->dev, "%s()\n", __func__);

	if (sb->state.system_state == SMARTBATTERY_SYSTEM_READY) {
		dev_info(&sb->device.client->dev, "Going to Updater\n");
		rc = smartbattery_start_upd(&sb->device, &sb->state,
				RESET_TIMEOUT_MS);
		if (rc != 0) {
			dev_err(&sb->device.client->dev,
					"Error going to Updater\n");
			goto out;
		}
	}
	if (sb->state.system_state != SMARTBATTERY_SYSTEM_UPDATER) {
		dev_err(&sb->device.client->dev, "Cannot go to Updater\n");
		goto out;
	}
	ret = 0;
out:
	return ret;
}

static int go_to_application(struct smartbattery *sb)
{
	int ret = -1;
	int rc;

	dev_dbg(&sb->device.client->dev, "%s()\n", __func__);

	if (sb->state.system_state == SMARTBATTERY_SYSTEM_UPDATER) {
		dev_info(&sb->device.client->dev, "Going to Application\n");
		rc = smartbattery_start_app(&sb->device, &sb->state,
				RESET_TIMEOUT_MS);
		if (rc != 0) {
			dev_err(&sb->device.client->dev,
					"Error going to Application\n");
			goto out;
		}
	}
	if (sb->state.system_state != SMARTBATTERY_SYSTEM_READY) {
		dev_err(&sb->device.client->dev, "Cannot go to Application\n");
		goto out;
	}
	ret = 0;
out:
	return ret;
}

static int go_to_update_mode_for(
	struct smartbattery *sb,
	enum smartbattery_partition_type type)
{
	int ret = -1;
	int rc;

	dev_dbg(&sb->device.client->dev, "%s(%d)\n", __func__, type);

	switch(type) {
	case SMARTBATTERY_PARTITION_APPLICATION:
		rc = go_to_updater(sb);
		if (rc != 0) {
			dev_err(&sb->device.client->dev,
					"Cannot go to Updater\n");
			goto out;
		}
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_UPDATER:
		rc = go_to_application(sb);
		if (rc != 0) {
			dev_err(&sb->device.client->dev,
					"Cannot go to Application\n");
			goto out;
		}
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_BOOTLOADER:
		break;
	}
out:
	return ret;
}

static int go_to_normal_mode_for(
	struct smartbattery *sb,
	enum smartbattery_partition_type type)
{
	int ret = -1;
	int rc;

	dev_dbg(&sb->device.client->dev, "%s(%d)\n", __func__, type);

	switch(type) {
	case SMARTBATTERY_PARTITION_APPLICATION:
		rc = go_to_application(sb);
		if (rc != 0) {
			dev_err(&sb->device.client->dev,
					"Cannot go to Application\n");
			goto out;
		}
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_UPDATER:
		rc = smartbattery_reset(&sb->device, &sb->state,
				RESET_TIMEOUT_MS);
		if (rc != 0) {
			dev_err(&sb->device.client->dev, "Cannot reset\n");
			goto out;
		}
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_BOOTLOADER:
		break;
	}
out:
	return ret;
}

static int update_firmware(
	struct smartbattery *sb,
	enum smartbattery_partition_type type,
	const uint8_t *data,
	size_t size,
	uint16_t address)
{
	int ret = -EIO;
	int rc;
	struct smartbattery_flash_area area;

	dev_dbg(&sb->device.client->dev,
			"%s(%d, size=%zu address=0x%.4x)\n",
			__func__, type, size, address);

	rc = go_to_update_mode_for(sb, type);
	if (rc < 0) {
		dev_err(&sb->device.client->dev,
				"Cannot go to Update mode\n");
		goto out;
	}

	area.address = address;
	area.length = size;
	rc = smartbattery_erase_flash(&sb->device, &area);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Erase failed (0x%.4x/%u)\n",
				area.address, area.length);
		goto out;
	}

	area.address = address;
	area.length = size;
	dev_info(&sb->device.client->dev, "Area: 0x%.4x / %zu\n",
			address, size);
	rc = smartbattery_write_data(&sb->device, &area, data);
	if (rc != 0) {
		dev_err(&sb->device.client->dev, "Update Data failed\n");
		goto out;
	}
	dev_info(&sb->device.client->dev, "Area updated");

	rc = go_to_normal_mode_for(sb, type);
	if (rc < 0) {
		dev_err(&sb->device.client->dev,
				"Cannot go to normal mode\n");
		goto out;
	}

	ret = 0;
out:
	return ret;
}

static uint16_t get_u16(const uint8_t *ptr)
{
	uint16_t ret;

	ret = *(uint16_t*)ptr;
	ret = le16_to_cpu(ret);

	return ret;
}

/* return code:
 * -1: error
 *  0: FW is up-to-date
 *  1: FW should be updated
 *  2: FW should be updated if downgrade were allowed
 */
static int should_update(
		struct smartbattery *sb,
		const uint8_t* fw_data,
		int fw_size,
		uint8_t major_current,
		uint8_t minor_current,
		uint8_t patch_current,
		uint8_t variant_current,
		unsigned int version_offset,
		uint16_t crc_current,
		unsigned int crc_offset,
		bool allow_downgrade)
{
	int ret = -1;
	uint16_t crc_computed;
	uint16_t crc_in_file;
	uint8_t major_in_file;
	uint8_t minor_in_file;
	uint8_t patch_in_file;
	uint8_t variant_in_file;
	uint32_t version_current;
	uint32_t version_in_file;

	dev_dbg(&sb->device.client->dev,
		"%s(%p, %p, %d, %u.%u.%u.%u 0x%x 0x%.4x 0x%x)\n",
		__func__,
		sb,
		fw_data,
		fw_size,
		major_current,
		minor_current,
		patch_current,
		variant_current,
		version_offset,
		crc_current,
		crc_offset);
	/* CRC size = 2 bytes */
	crc_computed = crc16_compute(fw_data, fw_size - 2);
	crc_in_file = get_u16(&fw_data[crc_offset]);

	major_in_file = fw_data[version_offset];
	minor_in_file = fw_data[version_offset+1];
	patch_in_file = fw_data[version_offset+2];
	variant_in_file = fw_data[version_offset+3];

	dev_info(&sb->device.client->dev, "CRC: 0x%.4x vs 0x%.4x vs 0x%.4x\n",
			crc_current,
			crc_in_file,
			crc_computed);

	dev_info(&sb->device.client->dev, "Version: %u.%u.%u.%u vs "
			"%u.%u.%u.%u\n",
			major_current,
			minor_current,
			patch_current,
			variant_current,
			major_in_file,
			minor_in_file,
			patch_in_file,
			variant_in_file);

	if (variant_current > 0 && variant_current < 255) {
		dev_info(&sb->device.client->dev,
				"Not updating FW with variant\n");
		ret = 0;
		goto out;
	}
	if (variant_in_file > 0 && variant_in_file < 255) {
		dev_info(&sb->device.client->dev,
				"Not updating with FW with variant\n");
		ret = 0;
		goto out;
	}

	version_current = major_current;
	version_current <<= 8;
	version_current |= minor_current;
	version_current <<= 8;
	version_current |= patch_current;
	version_current <<= 8;
	/* we don't check variant */
	version_in_file = major_in_file;
	version_in_file <<= 8;
	version_in_file |= minor_in_file;
	version_in_file <<= 8;
	version_in_file |= patch_in_file;
	version_in_file <<= 8;
	/* we don't check variant */

	if (crc_computed != crc_in_file) {
		dev_err(&sb->device.client->dev, "Firmware file corrupted\n");
		goto out;
	}
	/* by default, we don't update */
	ret = 0;
	if (version_in_file != version_current) {
		dev_info(&sb->device.client->dev, "Versions are different\n");
		ret = 1;
		/* dev version (0.0) is always allowed even in case of downgrade
		 */
		if (version_in_file == 0) {
			dev_info(&sb->device.client->dev,
					"Development version\n");
		} else if (version_current == 0xffffff00) {
			/* just to be sure erased version will be updated */
			dev_info(&sb->device.client->dev, "Broken version\n");
		} else if (version_in_file < version_current) {
			dev_info(&sb->device.client->dev,
				"Downgrade is %sallowed\n",
				allow_downgrade ? "" : "not ");
			if (!allow_downgrade)
				ret = 2;
		}
		goto out;
	}
	if (crc_computed != crc_current) {
		dev_info(&sb->device.client->dev, "CRCs are different\n");
		ret = 1;
	}
out:
	return ret;
}

static int get_fw_info(
		struct smartbattery *sb,
		struct smartbattery_flash_info *flash_info,
		enum smartbattery_partition_type type,
		struct smartbattery_partition **partition_p,
		int *fw_offset_p,
		int *fw_size_p,
		uint8_t *major_p,
		uint8_t *minor_p,
		uint8_t *patch_p,
		uint8_t *variant_p,
		uint16_t *crc_current_p)
{
	int ret = -EINVAL;

	dev_dbg(&sb->device.client->dev, "%s(%d)\n", __func__, type);

	switch(type) {
	case SMARTBATTERY_PARTITION_APPLICATION:
		*partition_p = &sb->partition_application;
		*fw_offset_p = sb->partition_application.begin -
			flash_info->flash_begin;
		*fw_size_p = sb->partition_application.size;
		*major_p = sb->version.application_version.major;
		*minor_p = sb->version.application_version.minor;
		*patch_p = sb->version.application_version.patch;
		*variant_p = sb->version.application_version.variant;
		*crc_current_p = sb->version.application_crc16;
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_UPDATER:
		*partition_p = &sb->partition_updater;
		*fw_offset_p = sb->partition_updater.begin -
			flash_info->flash_begin;
		*fw_size_p = sb->partition_updater.size;
		*major_p = sb->version.updater_version.major;
		*minor_p = sb->version.updater_version.minor;
		*patch_p = sb->version.updater_version.patch;
		*variant_p = sb->version.updater_version.variant;
		*crc_current_p = sb->version.updater_crc16;
		ret = 0;
		break;
	case SMARTBATTERY_PARTITION_BOOTLOADER:
		break;
	}
	return ret;
}

static int update_fw_partition(
		struct smartbattery *sb,
		struct smartbattery_flash_info *flash_info,
		const struct firmware *fw_entry,
		const char *fw_name,
		enum smartbattery_partition_type type,
		bool allow_downgrade,
		bool force_update)
{
	int ret = -EIO;
	struct smartbattery_partition *partition;
	int rc;
	const uint8_t *fw_data;
	int fw_offset;
	int fw_size;
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	uint8_t variant;
	unsigned int version_offset;
	uint16_t crc_current;
	unsigned int crc_offset;
	bool to_update = force_update;

	dev_dbg(&sb->device.client->dev, "%s(%d, force_update=%d)\n",
			__func__,
			type,
			force_update);

	fw_data = fw_entry->data;

	rc = get_fw_info(sb, flash_info, type, &partition,
			&fw_offset, &fw_size,
			&major, &minor, &patch, &variant, &crc_current);
	if (rc != 0)
		goto out;

	dev_dbg(&sb->device.client->dev, "fw_offset=0x%x fw_size=%u\n",
			fw_offset, fw_size);

	/* if update is not forced, we check if we should update*/
	if (!force_update) {
		version_offset = partition->version_offset - partition->begin;
		crc_offset = partition->crc_offset - partition->begin;

		rc = should_update(
				sb,
				fw_data + fw_offset, fw_size,
				major, minor, patch, variant, version_offset,
				crc_current, crc_offset, allow_downgrade);
		if (rc == 1) {
			to_update = true;
			dev_info(&sb->device.client->dev,
					"Going to update firmware \"%s\"\n",
					fw_name);
		} else if (rc == 0) {
			dev_info(&sb->device.client->dev,
					"Firmware is up-to-date\n");
			ret = 0;
		} else if (rc == 2) {
			dev_info(&sb->device.client->dev,
					"Firmware won't be updated\n");
			ret = 0;
		} else {
			dev_err(&sb->device.client->dev, "Error\n");
		}
	}
	if (to_update) {
		rc = update_firmware(
				sb,
				type,
				fw_data + fw_offset,
				fw_size,
				partition->begin);
		if (rc < 0)
			goto out;
		smartbattery_read_info(sb);
		ret = 0;
	}
out:
	return ret;
}

int smartbattery_upload_firmware(
		struct smartbattery *sb,
		const char *fw_name,
		bool allow_downgrade)
{
	int ret = -EIO;
	const struct firmware *fw_entry;
	int rc;
	struct smartbattery_flash_info flash_info;

	rc = request_firmware(&fw_entry,
			fw_name,
			&sb->device.client->dev);
	if (rc) {
		dev_warn(&sb->device.client->dev,
				"Firmware \"%s\" not found\n",
				fw_name);
		goto out;
	}

	rc = smartbattery_get_flash_info(
			sb->ctrl_info.type,
			&flash_info);
	if (rc) {
		dev_err(&sb->device.client->dev, "Cannot get flash info\n");
		goto out_release;
	}

	if (fw_entry->size != flash_info.flash_size) {
		dev_warn(&sb->device.client->dev,
			"Firmware \"%s\" hasn't the expected size\n",
				fw_name);
		dev_warn(&sb->device.client->dev,
			"  %d != %d\n",
				(int)fw_entry->size, flash_info.flash_size);
		goto out_release;
	}

	/* we cannot handle that case */
	if (!sb->application_valid && !sb->updater_valid) {
		dev_crit(&sb->device.client->dev,
				"Application and Updater are not valid\n");
		goto out_release;
	}

	/* firstly, update non-valid partition */
	if (!sb->application_valid) {
		rc = update_fw_partition(
				sb,
				&flash_info,
				fw_entry,
				fw_name,
				SMARTBATTERY_PARTITION_APPLICATION,
				allow_downgrade,
				true);
		if (rc)
			goto out_release;
		rc = smartbattery_check_partition(
				&sb->device,
				"Application",
				&sb->partition_application,
				sb->version.application_crc16,
				&sb->application_valid);
		if (rc)
			goto out_release;
	}
	if (!sb->updater_valid) {
		rc = update_fw_partition(
				sb,
				&flash_info,
				fw_entry,
				fw_name,
				SMARTBATTERY_PARTITION_UPDATER,
				allow_downgrade,
				true);
		if (rc)
			goto out_release;
		rc = smartbattery_check_partition(
				&sb->device,
				"Updater",
				&sb->partition_updater,
				sb->version.updater_crc16,
				&sb->updater_valid);
		if (rc)
			goto out_release;
	}

	/* partitions should be valid now */
	if (sb->updater_valid) {
		rc = update_fw_partition(
				sb,
				&flash_info,
				fw_entry,
				fw_name,
				SMARTBATTERY_PARTITION_APPLICATION,
				allow_downgrade,
				false);
		if (rc)
			goto out_release;
		rc = smartbattery_check_partition(
				&sb->device,
				"Application",
				&sb->partition_application,
				sb->version.application_crc16,
				&sb->application_valid);
		if (rc)
			goto out_release;
	}

	if (sb->application_valid) {
		rc = update_fw_partition(
				sb,
				&flash_info,
				fw_entry,
				fw_name,
				SMARTBATTERY_PARTITION_UPDATER,
				allow_downgrade,
				false);
		if (rc)
			goto out_release;
		rc = smartbattery_check_partition(
				&sb->device,
				"Updater",
				&sb->partition_updater,
				sb->version.updater_crc16,
				&sb->updater_valid);
		if (rc)
			goto out_release;
	}

	ret = 0;
out_release:
	release_firmware(fw_entry);
out:
	return ret;
}
