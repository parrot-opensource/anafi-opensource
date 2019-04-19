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
#include "smartbattery_common.h"
#include "smartbattery_misc.h"
#include "smartbattery_tps.h"

#define REGISTER_DID                           0x01
#define REGISTER_MODE                          0x03
#define REGISTER_DEVICE_INFO                   0x2F

static int tps_read_reg(
	struct smartbattery *sb,
	uint8_t reg,
	void *rx_data,
	int rx_len)
{
	int ret;
	struct smartbattery_i2c_request i2c_request;

	i2c_request.component = SMARTBATTERY_USB;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = rx_len+1;
	i2c_request.tx_data[0] = reg;

	ret = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (ret < 0)
		goto out;

	memcpy(rx_data, i2c_request.rx_data+1, rx_len);

out:
	return ret;
}

int smartbattery_tps_get_device_id(
	struct smartbattery *sb,
	uint32_t *value_p)
{
	int ret;
	uint8_t val[4];

	ret = tps_read_reg(sb, REGISTER_DID, val, sizeof(val));
	if (ret < 0)
		goto out;

	*value_p = get_le32(val);
out:
	return ret;
}

int smartbattery_tps_get_mode(
	struct smartbattery *sb,
	char *value,
	size_t size)
{
	int ret;

	if (size != 5)
		return -EINVAL;

	ret = tps_read_reg(sb, REGISTER_MODE, value, 4);

	value[4] = 0;

	return ret;
}

int smartbattery_tps_get_device_info(
	struct smartbattery *sb,
	char *value,
	size_t size)
{
	int ret;

	if (size != 9)
		return -EINVAL;

	ret = tps_read_reg(sb, REGISTER_DEVICE_INFO, value, 8);

	value[8] = 0;

	return ret;
}
