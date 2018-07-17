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

#include <linux/power_supply.h>

#include "smartbattery_common.h"
#include "smartbattery_property.h"
#include "smartbattery_device.h"

/* 0.1°K to 0.1°C */
#define TEMP_TENTH_KELVIN_TO_TENTH_CELSIUS  2731

static enum power_supply_property smartbattery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_HEALTH,
};

static const int alert_to_health[] = {
	[SMARTBATTERY_ALERT_TEMP_NONE]          = POWER_SUPPLY_HEALTH_GOOD,
	[SMARTBATTERY_ALERT_TEMP_HIGH_CRITICAL] = POWER_SUPPLY_HEALTH_OVERHEAT,
	[SMARTBATTERY_ALERT_TEMP_HIGH_WARNING]  = POWER_SUPPLY_HEALTH_OVERHEAT,
	[SMARTBATTERY_ALERT_TEMP_LOW_CRITICAL]  = POWER_SUPPLY_HEALTH_COLD,
	[SMARTBATTERY_ALERT_TEMP_LOW_WARNING]   = POWER_SUPPLY_HEALTH_COLD,
};

static int smartbattery_get_property(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smartbattery *sb;
	int value;

	sb = power_supply_get_drvdata(psy);

	if ((!sb->present) && psp != POWER_SUPPLY_PROP_PRESENT)
		return -ENODEV;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		switch (sb->state.battery_mode) {
		case SMARTBATTERY_DISCHARGING:
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
			break;
		case SMARTBATTERY_CHARGING:
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
			break;
		case SMARTBATTERY_FULLY_CHARGED:
			val->intval = POWER_SUPPLY_STATUS_FULL;
			break;
		default:
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			break;
		}
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = sb->present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		if (sb->chemistry.type == SMARTBATTERY_TYPE_LIPO)
			val->intval = POWER_SUPPLY_TECHNOLOGY_LIPO;
		else
			val->intval = POWER_SUPPLY_TECHNOLOGY_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = sb->device_name;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = sb->manufacturer.name;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		/* mV to µV */
		val->intval = sb->gauge.voltage.value * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		/* mA to µA */
		val->intval = sb->gauge.current_val.value * 1000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		/* mA to µA */
		val->intval = sb->gauge.avg_current.value * 1000;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		/* 0.1°K to 0.1°C */
		val->intval = sb->gauge.temperature.value -
			TEMP_TENTH_KELVIN_TO_TENTH_CELSIUS;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* percentage */
		val->intval = sb->gauge.rsoc.value;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		/* min to sec */
		value = sb->gauge.avg_time_to_empty.value;
		if (value == 0xffff)
			val->intval = -1;
		else
			val->intval = value * 60;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		/* min to sec */
		value = sb->gauge.avg_time_to_full.value;
		if (value == 0xffff)
			val->intval = -1;
		else
			val->intval = value * 60;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		/* mAh to µAh */
		val->intval = sb->gauge.full_charge_cap.value * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		/* mAh to µAh */
		val->intval = sb->gauge.remaining_cap.value * 1000;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		if (sb->alerts.gauge < COUNT_OF(alert_to_health))
			val->intval = alert_to_health[sb->alerts.gauge];
		else
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int smartbattery_property_fill(struct power_supply_desc *desc)
{
	desc->properties = smartbattery_props;
	desc->num_properties = ARRAY_SIZE(smartbattery_props);
	desc->get_property = smartbattery_get_property;

	return 0;
}
