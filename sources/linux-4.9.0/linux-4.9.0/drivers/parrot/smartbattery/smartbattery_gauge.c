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

#include <linux/delay.h>

#include "smartbattery_device.h"
#include "smartbattery_gauge.h"
#include "smartbattery_misc.h"

#define GAUGE_DESIGN_CAPACITY 0x3C
#define GAUGE_MAC 0x3E
#define GAUGE_MAC_DEVICE_TYPE 0x0001
#define GAUGE_MAC_FW_VERSION 0x0002
#define GAUGE_MAC_STATIC_DF_SIGNATURES 0x0005
#define GAUGE_MAC_CHEMICAL_ID 0x0006
#define GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES 0x0008
#define GAUGE_MAC_DATE 0x004D

static int smartbattery_gauge_get_reg(
	struct smartbattery *sb,
	uint8_t reg,
	uint16_t *value_p)
{
	int ret = -ENODEV;
	int rc;
	struct smartbattery_i2c_request i2c_request;
	uint8_t rx_data[2];

	if (!sb->present)
		goto out;

	ret = -EIO;

	/* */
	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 1;
	i2c_request.tx_data[0] = reg;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc < 0)
		goto out;
	rx_data[1] = i2c_request.rx_data[0];

	/* */
	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 1;
	i2c_request.tx_data[0] = reg + 1;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc < 0)
		goto out;
	rx_data[0] = i2c_request.rx_data[0];

	*value_p = rx_data[0];
	*value_p <<= 8;
	*value_p |= rx_data[1];

	ret = 0;
out:
	return ret;
}

static int smartbattery_gauge_get_mac(
	struct smartbattery* sb,
	uint16_t command,
	void *result,
	size_t size,
	int delay_ms)
{
	int ret = -ENODEV;
	int rc;
	struct smartbattery_i2c_request i2c_request;

	if (!sb->present)
		goto out;

	ret = -EIO;

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 3;
	i2c_request.rx_length = 0;
	i2c_request.tx_data[0] = GAUGE_MAC;
	i2c_request.tx_data[1] = command;
	i2c_request.tx_data[2] = command >> 8;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc < 0)
		goto out;

	if (delay_ms > 0)
		msleep(delay_ms);

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = size + 2;
	i2c_request.tx_data[0] = GAUGE_MAC;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc < 0)
		goto out;

	if (i2c_request.rx_data[0] != command ||
			i2c_request.rx_data[1] != (command >> 8))
		goto out;

	memcpy(result, i2c_request.rx_data + 2, size);

	ret = 0;
out:
	return ret;
}

int smartbattery_gauge_get_fw_version(struct smartbattery* sb)
{
	int ret;

	ret = smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_FW_VERSION,
		sb->gauge_fw_version,
		sizeof(sb->gauge_fw_version),
		0);

	if (ret < 0)
		goto out;

	dev_info(&sb->device.client->dev,
			"Gauge FW Version: "
			"%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x\n",
                        sb->gauge_fw_version[0],
                        sb->gauge_fw_version[1],
                        sb->gauge_fw_version[2],
                        sb->gauge_fw_version[3],
                        sb->gauge_fw_version[4],
                        sb->gauge_fw_version[5],
                        sb->gauge_fw_version[6],
                        sb->gauge_fw_version[7],
                        sb->gauge_fw_version[8],
                        sb->gauge_fw_version[9]);

	dev_info(&sb->device.client->dev, "  Gauge Version: %x.%x\n",
                        sb->gauge_fw_version[2],
                        sb->gauge_fw_version[3]);
out:
	return ret;
}

int smartbattery_gauge_get_signatures(struct smartbattery* sb)
{
	int ret;
	uint8_t data[2];

	/* Technical Reference bq28z610
	 * sluua65b, page 65: "wait time of 250 ms" */
	ret = smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_STATIC_DF_SIGNATURES,
		data,
		sizeof(data),
		250);

	if (ret < 0)
		goto out;

	sb->gauge_signatures[0] = get_le16(&data[0]);

	/* */

	/* Technical Reference bq28z610
	 * sluua65b, page 65: "wait time of 250 ms" */
	ret = smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES,
		data,
		sizeof(data),
		250);

	if (ret < 0)
		goto out;

	sb->gauge_signatures[1] = get_le16(&data[0]);

	dev_info(&sb->device.client->dev,
			"Gauge Signatures: 0x%.4x 0x%.4x\n",
                        sb->gauge_signatures[0],
                        sb->gauge_signatures[1]);

out:
	return ret;
}

int smartbattery_gauge_get_date(struct smartbattery* sb)
{
	int ret;
	uint8_t data[2];
	uint8_t day;
	uint8_t month;
	uint16_t year;

	ret = smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_DATE,
		data,
		sizeof(data),
		0);

	if (ret < 0)
		goto out;

	sb->gauge_date = get_le16(&data[0]);

	smartbattery_gauge_value2date(sb->gauge_date, &day, &month, &year);

	dev_info(&sb->device.client->dev,
			"Gauge Date: %.2d/%.2d/%.4d\n",
			day,
			month,
			year);
out:
	return ret;
}

int smartbattery_gauge_get_design_capacity(
	struct smartbattery *sb,
	uint16_t *value_p)
{
	return smartbattery_gauge_get_reg(sb, GAUGE_DESIGN_CAPACITY, value_p);
}

int smartbattery_gauge_get_device_type(
	struct smartbattery *sb,
	uint16_t *value_p)
{
	return smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_DEVICE_TYPE,
		value_p,
		sizeof(*value_p),
		0);
}

int smartbattery_gauge_get_chem_id(struct smartbattery *sb, uint16_t *value_p)
{
	return smartbattery_gauge_get_mac(
		sb,
		GAUGE_MAC_CHEMICAL_ID,
		value_p,
		sizeof(*value_p),
		0);
}
