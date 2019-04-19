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

#include "smartbattery.h"
#include "smartbattery_device.h"
#include "smartbattery_charger.h"

#define REG_OPTION_0 0x00
#define REG_MANUFACTURER_ID 0x2E
#define REG_DEVICE_ID 0x2F

static int charger_read_u8(
	struct smartbattery *sb,
	uint8_t reg,
	uint8_t *value_p)
{
	int ret = -ENODEV;
	struct smartbattery_i2c_request i2c_request;

	if (!sb->present)
		goto out;

	i2c_request.component = SMARTBATTERY_CHARGER;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 1;
	i2c_request.tx_data[0] = reg;

	ret = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (ret < 0)
		goto out;

	*value_p = i2c_request.rx_data[0];

out:
	return ret;
}

static int charger_read_u16(
	struct smartbattery *sb,
	uint8_t reg,
	uint16_t *value_p)
{
	int ret = -ENODEV;
	struct smartbattery_i2c_request i2c_request;

	if (!sb->present)
		goto out;

	i2c_request.component = SMARTBATTERY_CHARGER;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 2;
	i2c_request.tx_data[0] = reg;

	ret = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (ret < 0)
		goto out;

	*value_p = i2c_request.rx_data[1];
	*value_p <<= 8;
	*value_p |= i2c_request.rx_data[0];

out:
	return ret;
}

int smartbattery_charger_get_device_id(
	struct smartbattery *sb,
	uint8_t *value_p)
{
	return charger_read_u8(sb, REG_DEVICE_ID, value_p);
}

int smartbattery_charger_get_manufacturer_id(
	struct smartbattery *sb,
	uint8_t *value_p)
{
	return charger_read_u8(sb, REG_MANUFACTURER_ID, value_p);
}

int smartbattery_charger_get_option0(
	struct smartbattery *sb,
	uint16_t *value_p)
{
	return charger_read_u16(sb, REG_OPTION_0, value_p);
}
