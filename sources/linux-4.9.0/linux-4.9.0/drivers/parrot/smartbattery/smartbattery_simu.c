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


#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>

#include "smartbattery_device.h"
#include "smartbattery_simu.h"

static ssize_t show_simu_enable(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	return sprintf(buf, "%s\n", (sb->simu_enable) ? "on" : "off");
}

static ssize_t store_simu_enable(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (!strncmp(buf, "on", 2)) {
		dev_info(&sb->device.client->dev, "Going to Simulation Mode\n");
		sb->simu_enable = true;
		sb->simu_rsoc = sb->gauge.rsoc.value;
		sb->simu_voltage = sb->gauge.voltage.value;
	} else if (!strncmp(buf, "off", 3)) {
		dev_info(&sb->device.client->dev, "Going to Real Mode\n");
		sb->simu_enable = false;
	} else
		return -EINVAL;

	return count;
}

static ssize_t show_simu_rsoc(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (!sb->simu_enable)
		return -EINVAL;

	return sprintf(buf, "%d\n", sb->simu_rsoc);
}

static ssize_t store_simu_rsoc(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	long val;
	int rc;

	if (!sb->present)
		return -ENODEV;

	if (!sb->simu_enable)
		return -EINVAL;

	rc = kstrtol(buf, 10, &val);
	if (rc < 0)
		return rc;

	if (val < 0 || val > 100)
		return -EINVAL;

	sb->simu_rsoc = val;

	return count;
}

static ssize_t show_simu_voltage(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	struct smartbattery *sb = dev_get_drvdata(dev);

	if (!sb->present)
		return -ENODEV;

	if (!sb->simu_enable)
		return -EINVAL;

	return sprintf(buf, "%d\n", sb->simu_voltage);
}

static ssize_t store_simu_voltage(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct smartbattery *sb = dev_get_drvdata(dev);
	long val;
	int rc;

	if (!sb->present)
		return -ENODEV;

	if (!sb->simu_enable)
		return -EINVAL;

	rc = kstrtol(buf, 10, &val);
	if (rc < 0)
		return rc;

	sb->simu_voltage = val;

	return count;
}

static DEVICE_ATTR(simu_enable, S_IRUSR|S_IWUSR,
		show_simu_enable, store_simu_enable);
static DEVICE_ATTR(simu_rsoc, S_IRUSR|S_IWUSR,
		show_simu_rsoc, store_simu_rsoc);
static DEVICE_ATTR(simu_voltage, S_IRUSR|S_IWUSR,
		show_simu_voltage, store_simu_voltage);

static struct attribute *simu_attrs[] = {
	&dev_attr_simu_enable.attr,
	&dev_attr_simu_rsoc.attr,
	&dev_attr_simu_voltage.attr,
	NULL,
};

static struct attribute_group simu_attr_group = {
	.attrs = simu_attrs,
};

int smartbattery_simu_init(struct smartbattery *sb)
{
	return sysfs_create_group(
			&sb->device.client->dev.kobj,
			&simu_attr_group);
}

void smartbattery_simu_exit(struct smartbattery *sb)
{
	sysfs_remove_group(
			&sb->device.client->dev.kobj,
			&simu_attr_group);
}
