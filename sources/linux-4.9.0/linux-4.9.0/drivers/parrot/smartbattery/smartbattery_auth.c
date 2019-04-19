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

#include <linux/random.h>

#include "smartbattery_device.h"
#include "smartbattery.h"
#include "smartbattery_common.h"
#include "smartbattery_gauge.h"
#include "smartbattery_misc.h"
#include "smartbattery_charger.h"
#include "smartbattery_tps.h"
#include "smartbattery_auth.h"

typedef bool(*auth_function_t)(struct smartbattery *sb);

static bool gauge_check_design_capacity(struct smartbattery *sb)
{
	bool ret = false;
	uint16_t value;
	int rc;

	rc = smartbattery_gauge_get_design_capacity(sb, &value);
	if (rc < 0)
		goto out;

	if (!strcmp(sb->manufacturer.name, "Fullymax"))
		ret = (value == 2550);
	else
		ret = (value == 2700);

out:
	return ret;
}

static bool gauge_check_device_type(struct smartbattery *sb)
{
	bool ret = false;
	uint16_t value;
	int rc;

	rc = smartbattery_gauge_get_device_type(sb, &value);
	if (rc < 0)
		goto out;

	ret = (value == 0x2610);

out:
	return ret;
}

static bool gauge_check_chem_id(struct smartbattery *sb)
{
	bool ret = false;
	uint16_t value;
	int rc;

	rc = smartbattery_gauge_get_chem_id(sb, &value);
	if (rc < 0)
		goto out;

	if (!strcmp(sb->manufacturer.name, "Fullymax"))
		ret = (value == 0x1682);
	else
		ret = (value == 0x3787);

out:
	return ret;
}

static bool charger_check_device_id(struct smartbattery *sb)
{
	bool ret = false;
	uint8_t value;
	int rc;

	rc = smartbattery_charger_get_device_id(sb, &value);
	if (rc < 0)
		goto out;

	ret = (value == 0x78);

out:
	return ret;
}

static bool charger_check_manufacturer_id(struct smartbattery *sb)
{
	bool ret = false;
	uint8_t value;
	int rc;

	rc = smartbattery_charger_get_manufacturer_id(sb, &value);
	if (rc < 0)
		goto out;

	ret = (value == 0x40);

out:
	return ret;
}

static bool charger_check_option0(struct smartbattery *sb)
{
	bool ret = false;
	uint16_t value;
	int rc;

	rc = smartbattery_charger_get_option0(sb, &value);
	if (rc < 0)
		goto out;

	/* with CHRG_INHIBIT*/
	ret = ((value & 0xfffe) == 0x860e);

out:
	return ret;
}

static bool tps_check_device_id(struct smartbattery *sb)
{
	bool ret = false;
	uint32_t value;
	int rc;

	rc = smartbattery_tps_get_device_id(sb, &value);
	if (rc < 0)
		goto out;

	ret = (value == 0x32454341);

out:
	return ret;
}

static bool tps_check_device_info(struct smartbattery *sb)
{
	bool ret = false;
	char value[] = "xxxxxxxx";
	int rc;

	rc = smartbattery_tps_get_device_info(sb, value, sizeof(value));
	if (rc < 0)
		goto out;

	ret = (strcmp(value, "TPS65988") == 0);

out:
	return ret;
}

static bool tps_check_mode(struct smartbattery *sb)
{
	bool ret = false;
	char value[] = "xxxx";
	int rc;

	rc = smartbattery_tps_get_mode(sb, value, sizeof(value));
	if (rc < 0)
		goto out;

	ret = (strcmp(value, "APP ") == 0);

out:
	return ret;
}

static bool msp430_check_app_crc(struct smartbattery *sb)
{
	bool ret = false;
	int rc;
	struct smartbattery_flash_chunk chunk = {
		.address = 0xF5FE,
		.length = 2,
	};
	uint16_t crc;

	rc = smartbattery_read_flash(&sb->device, &chunk);
	if (rc < 0)
		goto out;

	crc = get_le16(&chunk.data[0]);

	ret = (crc == sb->version.application_crc16);

out:
	return ret;
}

static const auth_function_t auth_functions[] = {
	gauge_check_design_capacity,
	gauge_check_device_type,
	gauge_check_chem_id,
	charger_check_device_id,
	charger_check_manufacturer_id,
	charger_check_option0,
	tps_check_device_id,
	tps_check_device_info,
	tps_check_mode,
	msp430_check_app_crc,
};

static bool check_smartbattery(struct smartbattery *sb)
{
	bool ret = false;
	uint32_t number;
	int rc;
	unsigned int idx;
	auth_function_t function;
	struct smartbattery_mode mode;

	get_random_bytes(&number, sizeof(number));

	idx = number % COUNT_OF(auth_functions);

	function = auth_functions[idx];

	mode.system_mode = SMARTBATTERY_SYSTEM_MAINTENANCE;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc < 0) {
		dev_warn(&sb->device.client->dev,
				"Cannot go to Maintenance Mode\n");
		goto out;
	}

	ret = function(sb);

	mode.system_mode = SMARTBATTERY_SYSTEM_NORMAL;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc < 0) {
		dev_warn(&sb->device.client->dev,
				"Cannot go to Normal Mode\n");
		goto out;
	}

out:
	dev_dbg(&sb->device.client->dev, "auth function %d: %s\n",
		idx, ret ? "ok" : "ko");

	return ret;
}
bool smartbattery_auth_check(struct smartbattery *sb)
{
	bool ret = true;
	static int count;

	count++;

	if (count == 20)
		ret = check_smartbattery(sb);

	return ret;
}

bool smartbattery_auth_check_full(struct smartbattery *sb)
{
	bool ret = false;
	int rc;
	unsigned int idx;
	auth_function_t function;
	struct smartbattery_mode mode;

	mode.system_mode = SMARTBATTERY_SYSTEM_MAINTENANCE;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc < 0) {
		dev_warn(&sb->device.client->dev,
				"Cannot go to Maintenance Mode\n");
		goto out;
	}

	for (idx = 0; idx < COUNT_OF(auth_functions); idx++) {
		function = auth_functions[idx];
		ret = function(sb);
		if (!ret) {
			dev_warn(
				&sb->device.client->dev,
				"test %d fails\n", idx);
			break;
		}
	}

	mode.system_mode = SMARTBATTERY_SYSTEM_NORMAL;
	rc = smartbattery_set_mode(&sb->device, &mode);
	if (rc < 0) {
		dev_warn(&sb->device.client->dev,
				"Cannot go to Normal Mode\n");
		goto out;
	}
out:
	dev_info(&sb->device.client->dev, "auth_check_full: %s\n",
		ret ? "ok" : "ko");

	return ret;
}
