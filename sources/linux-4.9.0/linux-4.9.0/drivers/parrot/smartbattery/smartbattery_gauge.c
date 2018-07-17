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

#define GAUGE_MAC 0x3E
#define GAUGE_MAC_FW_VERSION 0x0002
#define GAUGE_MAC_STATIC_DF_SIGNATURES 0x0005
#define GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES 0x0008
#define GAUGE_MAC_DATE 0x004D

int smartbattery_get_gauge_fw_version(struct smartbattery* sb)
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
	i2c_request.tx_data[1] = GAUGE_MAC_FW_VERSION;
	i2c_request.tx_data[2] = GAUGE_MAC_FW_VERSION >> 8;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 12 + 2;
	i2c_request.tx_data[0] = GAUGE_MAC;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	if (i2c_request.rx_data[0] != GAUGE_MAC_FW_VERSION ||
			i2c_request.rx_data[1] != (GAUGE_MAC_FW_VERSION >> 8))
		goto out;

	memcpy(sb->gauge_fw_version, i2c_request.rx_data + 2,
		sizeof(sb->gauge_fw_version));

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

	ret = 0;
out:
	return ret;
}

int smartbattery_get_gauge_signatures(struct smartbattery* sb)
{
	int ret = -ENODEV;
	int rc;
	struct smartbattery_i2c_request i2c_request;

	if (!sb->present)
		goto out;

	ret = -EIO;

	/* */
	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 3;
	i2c_request.rx_length = 0;
	i2c_request.tx_data[0] = GAUGE_MAC;
	i2c_request.tx_data[1] = GAUGE_MAC_STATIC_DF_SIGNATURES;
	i2c_request.tx_data[2] = GAUGE_MAC_STATIC_DF_SIGNATURES >> 8;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	/* Technical Reference bq28z610
	 * sluua65b, page 65: "wait time of 250 ms" */
	msleep(250);

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 2 + 2;
	i2c_request.tx_data[0] = GAUGE_MAC;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	if (i2c_request.rx_data[0] != GAUGE_MAC_STATIC_DF_SIGNATURES ||
			i2c_request.rx_data[1] !=
				(GAUGE_MAC_STATIC_DF_SIGNATURES >> 8))
		goto out;

	sb->gauge_signatures[0] = get_le16(&i2c_request.rx_data[2]);

	/* */
	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 3;
	i2c_request.rx_length = 0;
	i2c_request.tx_data[0] = GAUGE_MAC;
	i2c_request.tx_data[1] = GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES;
	i2c_request.tx_data[2] = GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES >> 8;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	/* Technical Reference bq28z610
	 * sluua65b, page 65: "wait time of 250 ms" */
	msleep(250);

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 2 + 2;
	i2c_request.tx_data[0] = GAUGE_MAC;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	if (i2c_request.rx_data[0] != GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES ||
			i2c_request.rx_data[1] !=
				(GAUGE_MAC_STATIC_CHEM_DF_SIGNATURES >> 8))
		goto out;

	sb->gauge_signatures[1] = get_le16(&i2c_request.rx_data[2]);

	dev_info(&sb->device.client->dev,
			"Gauge Signatures: 0x%.4x 0x%.4x\n",
                        sb->gauge_signatures[0],
                        sb->gauge_signatures[1]);

	ret = 0;
out:
	return ret;
}

int smartbattery_get_gauge_date(struct smartbattery* sb)
{
	int ret = -ENODEV;
	int rc;
	struct smartbattery_i2c_request i2c_request;
	uint8_t day;
	uint8_t month;
	uint16_t year;

	if (!sb->present)
		goto out;

	ret = -EIO;

	/* */
	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 3;
	i2c_request.rx_length = 0;
	i2c_request.tx_data[0] = GAUGE_MAC;
	i2c_request.tx_data[1] = GAUGE_MAC_DATE;
	i2c_request.tx_data[2] = GAUGE_MAC_DATE >> 8;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	i2c_request.component = SMARTBATTERY_GAUGE;
	i2c_request.tx_length = 1;
	i2c_request.rx_length = 2 + 2;
	i2c_request.tx_data[0] = GAUGE_MAC;

	rc = smartbattery_i2c_bridge_async(&sb->device, &i2c_request);
	if (rc != 0)
		goto out;

	if (i2c_request.rx_data[0] != GAUGE_MAC_DATE ||
			i2c_request.rx_data[1] != (GAUGE_MAC_DATE >> 8))
		goto out;

	sb->gauge_date = get_le16(&i2c_request.rx_data[2]);

	smartbattery_gauge_value2date(sb->gauge_date, &day, &month, &year);

	dev_info(&sb->device.client->dev,
			"Gauge Date: %.2d/%.2d/%.4d\n",
			day,
			month,
			year);

	ret = 0;
out:
	return ret;
}
