/*
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * History:
 *	2014/07/21 - [Cao Rongrong] Create
 *
 * Copyright (C) 2014-2019, Ambarella, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/iio/iio.h>
#include <linux/iio/machine.h>
#include <linux/iio/driver.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/triggered_event.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/input.h>
#include <plat/rct.h>
#include <plat/adc.h>

/*
 * T = V * coeff - offset
 * T is Celsius degree
 * V is adc value read out
 * coeff varies from process to process, it should be 0.07 for 14nm
 * offset varies from chip to chip, it needs calibration
 */

/* 105000 is a rough value, it veries from chip to chip */
#define AMBARELLA_ADC_T2V_OFFSET	105000
/* Typical sensor slope coefficient at all temperatures */
#define AMBARELLA_ADC_T2V_COEFF		70

struct ambadc_keymap {
	u32 key_code;
	u32 channel : 4;
	u32 low_level : 12;
	u32 high_level : 12;
};

struct ambarella_adc {
	struct device *dev;
	void __iomem *regbase;
	struct regmap *reg_rct;
	int irq;
	struct clk *clk;
	u32 clk_rate;
	struct mutex mtx;
	struct iio_trigger *trig;
	unsigned long channels_mask;
	unsigned long scalers_mask; /* 1.8v if corresponding bit is set */
	unsigned long fifo_enable_mask;
	u32 vol_threshold[ADC_NUM_CHANNELS];
	u32 fifo_threshold;
	int t2v_channel;
	u32 t2v_offset;
	u32 t2v_coeff;

	/* following are for adc key if existed */
	struct input_dev *input;
	struct ambadc_keymap *keymap;
	u32 key_num;
	u32 key_pressed[ADC_NUM_CHANNELS]; /* save the key currently pressed */
};

static void ambarella_adc_set_threshold(struct ambarella_adc *ambadc);

static void ambarella_adc_key_filter_out(struct ambarella_adc *ambadc,
		struct ambadc_keymap *keymap)
{
	/* we expect the adc level which is out of current key's range. */
	ambadc->vol_threshold[keymap->channel] = ADC_INT_THRESHOLD_EN;
	ambadc->vol_threshold[keymap->channel] |= ADC_VAL_LO(keymap->low_level);
	ambadc->vol_threshold[keymap->channel] |= ADC_VAL_HI(keymap->high_level);
	ambarella_adc_set_threshold(ambadc);
}

static int ambarella_adc_key_handler(struct ambarella_adc *ambadc,
		u32 ch, u32 level)
{
	struct ambadc_keymap *keymap = ambadc->keymap;
	struct input_dev *input = ambadc->input;
	u32 i;

	for (i = 0; i < ambadc->key_num; i++, keymap++) {
		if (ch != keymap->channel
			|| level < keymap->low_level
			|| level > keymap->high_level)
			continue;

		if (ambadc->key_pressed[ch] == KEY_RESERVED
			&& keymap->key_code != KEY_RESERVED) {
			input_report_key(input, keymap->key_code, 1);
			input_sync(input);
			ambadc->key_pressed[ch] = keymap->key_code;
			ambarella_adc_key_filter_out(ambadc, keymap);

			dev_dbg(&input->dev, "key[%d:%d] pressed %d\n",
				ch, keymap->key_code, level);
			break;
		} else if (ambadc->key_pressed[ch] != KEY_RESERVED
			&& keymap->key_code == KEY_RESERVED) {
			input_report_key(input, ambadc->key_pressed[ch], 0);
			input_sync(input);
			ambadc->key_pressed[ch] = KEY_RESERVED;
			ambarella_adc_key_filter_out(ambadc, keymap);

			dev_dbg(&input->dev, "key[%d:%d] released %d\n",
				ch, keymap->key_code, level);
			break;
		}
	}

	return 0;
}

static int ambarella_adc_key_of_parse(struct ambarella_adc *ambadc)
{
	struct device_node *np = ambadc->dev->of_node;
	struct ambadc_keymap *keymap;
	const __be32 *prop;
	u32 propval, i, size;

	prop = of_get_property(np, "amb,keymap", &size);
	if (!prop || size % (sizeof(__be32) * 2)) {
		dev_err(ambadc->dev, "Invalid keymap!\n");
		return -ENOENT;
	}

	/* cells is 2 for each keymap */
	size /= sizeof(__be32) * 2;
	ambadc->key_num = size;

	ambadc->keymap = devm_kzalloc(ambadc->dev,
			sizeof(struct ambadc_keymap) * size, GFP_KERNEL);
	if (ambadc->keymap == NULL){
		dev_err(ambadc->dev, "No memory for keymap!\n");
		return -ENOMEM;
	}

	keymap = ambadc->keymap;

	for (i = 0; i < ambadc->key_num; i++, keymap++) {
		propval = be32_to_cpup(prop + i * 2);
		keymap->low_level = propval & 0xfff;
		keymap->high_level = (propval >> 16) & 0xfff;
		keymap->channel = propval >> 28;
		if (keymap->channel >= ADC_NUM_CHANNELS) {
			dev_err(ambadc->dev, "Invalid channel: %d\n", keymap->channel);
			return -EINVAL;
		}

		keymap->key_code = be32_to_cpup(prop + i * 2 + 1);

		if (keymap->key_code == KEY_RESERVED)
			ambarella_adc_key_filter_out(ambadc, keymap);

		input_set_capability(ambadc->input, EV_KEY, keymap->key_code);

		set_bit(keymap->channel, &ambadc->channels_mask);
	}

	for (i = 0; i < ADC_NUM_CHANNELS; i++)
		ambadc->key_pressed[i] = KEY_RESERVED;

	return 0;
}

static int ambarella_adc_key_init(struct ambarella_adc *ambadc)
{
	struct device_node *np = ambadc->dev->of_node;
	struct input_dev *input;
	int rval = 0;

	if (!of_find_property(np, "amb,keymap", NULL))
		return 0;

	input = devm_input_allocate_device(ambadc->dev);
	if (!input) {
		dev_err(ambadc->dev, "input_allocate_device fail!\n");
		return -ENOMEM;
	}

	input->name = "AmbADCkey";
	input->phys = "ambadckey/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = ambadc->dev;

	rval = input_register_device(input);
	if (rval < 0) {
		dev_err(ambadc->dev, "Register input_dev failed!\n");
		return rval;
	}

	ambadc->input = input;

	rval = ambarella_adc_key_of_parse(ambadc);
	if (rval < 0) {
		input_unregister_device(ambadc->input);
		return rval;
	}

	dev_info(ambadc->dev, "ADC key input driver probed!\n");

	return 0;
}


/*****************************************************************************/


static void ambarella_adc_set_threshold(struct ambarella_adc *ambadc)
{
	u32 i, tmp;

	for (i = 0; i < ADC_NUM_CHANNELS; i++) {
		tmp = ambadc->vol_threshold[i];
		writel_relaxed(tmp, ambadc->regbase + ADC_INT_CTRL_X_OFFSET(i));
	}
}

static void ambarella_adc_set_fifo(struct ambarella_adc *ambadc)
{
	u32 tmp, bit, chan_num, fifo_depth, fifo_threshold;

	if (bitmap_empty(&ambadc->fifo_enable_mask, ADC_NUM_CHANNELS))
		return;

	chan_num = bitmap_weight(&ambadc->fifo_enable_mask, ADC_NUM_CHANNELS);
	fifo_depth = (ADC_MAX_FIFO_DEPTH / chan_num) & ~0x3;
	fifo_threshold = (fifo_depth >> 2) * 3;

	for_each_set_bit(bit, &ambadc->fifo_enable_mask, ADC_NUM_CHANNELS) {
		tmp = ADC_FIFO_ID(bit) | ADC_FIFO_TH(fifo_threshold) | fifo_depth;
		writel_relaxed(tmp, ambadc->regbase + ADC_FIFO_CTRL_X_OFFSET(bit));
	}

	ambadc->fifo_threshold = fifo_threshold;

	/* clear fifo first */
	writel_relaxed(ADC_FIFO_CLEAR, ambadc->regbase + ADC_FIFO_CTRL_OFFSET);
}

static void ambarella_adc_power_up(struct ambarella_adc *ambadc)
{
	/* soft reset adc, all registers except for scaler reg will be cleared */
	writel_relaxed(ADC_CONTROL_RESET, ambadc->regbase + ADC_CONTROL_OFFSET);

	/* power up adc and all needed scaler */
	regmap_write(ambadc->reg_rct, ADC16_CTRL_OFFSET, ambadc->scalers_mask << 8);

	if (ambadc->t2v_channel >= 0)
		regmap_write(ambadc->reg_rct, T2V_CTRL_OFFSET, 0x0);
}

static void ambarella_adc_power_down(struct ambarella_adc *ambadc)
{
	u32 tmp;

	/* soft reset adc, all registers except for scaler reg will be cleared */
	writel_relaxed(ADC_CONTROL_RESET, ambadc->regbase + ADC_CONTROL_OFFSET);

	if (ambadc->t2v_channel >= 0)
		regmap_write(ambadc->reg_rct, T2V_CTRL_OFFSET, 0x1);

	/* power down adc and all scalers */
	tmp = ((1 << ADC_NUM_CHANNELS) - 1) << 8;
	regmap_write(ambadc->reg_rct, ADC16_CTRL_OFFSET, tmp | ADC_POWER_DOWN);
}

static void ambarella_adc_start(struct ambarella_adc *ambadc)
{
	u32 tmp;

	/* soft reset adc, all registers except for scaler reg will be cleared */
	writel_relaxed(ADC_CONTROL_RESET, ambadc->regbase + ADC_CONTROL_OFFSET);

	/* return directly if no channel need to be enabled */
	if (bitmap_empty(&ambadc->channels_mask, ADC_NUM_CHANNELS))
		return;

	/* enable adc, it must be done before parameters setting */
	tmp = ADC_CONTROL_MODE | ADC_CONTROL_ENABLE;
	writel_relaxed(tmp, ambadc->regbase + ADC_CONTROL_OFFSET);
	usleep_range(3, 10);

	/* we just use one slot */
	writel_relaxed(0x0, ambadc->regbase + ADC_SLOT_NUM_OFFSET);

	/* enable in used channels in each slot period */
	writel_relaxed(ambadc->channels_mask, ambadc->regbase + ADC_SLOT_CTRL_0_OFFSET);

	/* setup the period cycle for one slot */
	tmp = bitmap_weight(&ambadc->channels_mask, ADC_NUM_CHANNELS);
	tmp *= ADC_PERIOD_CYCLE;
	writel_relaxed(tmp - 1, ambadc->regbase + ADC_SLOT_PERIOD_OFFSET);

	ambarella_adc_set_threshold(ambadc);

	ambarella_adc_set_fifo(ambadc);

	/* start adc */
	tmp = ADC_CONTROL_MODE | ADC_CONTROL_ENABLE | ADC_CONTROL_START;
	writel_relaxed(tmp, ambadc->regbase + ADC_CONTROL_OFFSET);
}

static irqreturn_t ambarella_adc_irq(int irq, void *dev_id)
{
	struct iio_dev *indio_dev = dev_id;
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	u32 i, ctrl_int, data_int, data;

	ctrl_int = readl_relaxed(ambadc->regbase + ADC_CTRL_INTR_TABLE_OFFSET);
	data_int = readl_relaxed(ambadc->regbase + ADC_DATA_INTR_TABLE_OFFSET);

	if (readl_relaxed(ambadc->regbase + ADC_FIFO_INTR_TABLE_OFFSET)) {
		BUG_ON(!iio_buffer_enabled(indio_dev));
		disable_irq_nosync(irq);
		iio_trigger_poll(indio_dev->trig);
	}

	for_each_set_bit(i, (unsigned long *)&data_int, ADC_NUM_CHANNELS) {
		data = readl_relaxed(ambadc->regbase + ADC_DATA_X_OFFSET(i));

		if (ambadc->input)
			ambarella_adc_key_handler(ambadc, i, data);

		/*
		 * we don't know whether it is a upper or lower threshold
		 * event. Userspace will have to check the channel value if
		 * it wants to know.
		 */
		iio_push_event(indio_dev,
			IIO_UNMOD_EVENT_CODE(IIO_VOLTAGE, i,
				IIO_EV_TYPE_THRESH, IIO_EV_DIR_EITHER),
			iio_get_time_ns(indio_dev));
	}

	/* clear intr source at last to avoid dummy irq */
	writel_relaxed(ctrl_int, ambadc->regbase + ADC_CTRL_INTR_TABLE_OFFSET);
	writel_relaxed(data_int, ambadc->regbase + ADC_DATA_INTR_TABLE_OFFSET);

	return IRQ_HANDLED;
}

static int ambarella_adc_set_trigger_state(struct iio_trigger *trig, bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	struct iio_chan_spec const *chan;
	int bit;

	for_each_set_bit(bit, indio_dev->active_scan_mask, indio_dev->masklength) {
		chan = indio_dev->channels + bit;
		if (state)
			set_bit(chan->channel, &ambadc->fifo_enable_mask);
		else
			clear_bit(chan->channel, &ambadc->fifo_enable_mask);
	}

	ambarella_adc_start(ambadc);

	return 0;
}

static irqreturn_t ambarella_adc_trigger_handler(int irq, void *private)
{
	struct iio_poll_func *pf = (struct iio_poll_func *)private;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	u32 i, j, fifo_int, fifo_count;
	u16 data[ADC_FIFO_NUMBER];

	/* wait for all channels fifo to reach at threshold */
	while (1) {
		fifo_int = readl_relaxed(ambadc->regbase + ADC_FIFO_INTR_TABLE_OFFSET);
		if (fifo_int == ambadc->fifo_enable_mask)
			break;
	}

	fifo_count = ambadc->fifo_threshold;

	while (fifo_count) {
		j = 0;
		for_each_set_bit(i, (unsigned long *)&fifo_int, ADC_FIFO_NUMBER) {
			data[j] = readl_relaxed(ambadc->regbase + ADC_FIFO_DATA_X_OFFSET(i));
			j++;
		}
		iio_push_to_buffers(indio_dev, data);
		fifo_count--;
	}

	iio_trigger_notify_done(indio_dev->trig);

	/* clear intr source at last to avoid dummy irq */
	writel_relaxed(fifo_int, ambadc->regbase + ADC_FIFO_INTR_TABLE_OFFSET);

	enable_irq(ambadc->irq);

	return IRQ_HANDLED;
}

static const struct iio_trigger_ops ambarella_adc_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &ambarella_adc_set_trigger_state,
};

static int ambarella_adc_read_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int *val, int *val2, long mask)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	u32 tmp;

	switch (mask) {
	case IIO_CHAN_INFO_PROCESSED:
		/* only for temperature */
		if (!test_bit(chan->channel, &ambadc->channels_mask))
			return -EIO;

		if (chan->channel != ambadc->t2v_channel)
			return -EINVAL;

		mutex_lock(&ambadc->mtx);
		tmp = readl_relaxed(ambadc->regbase + ADC_DATA_X_OFFSET(chan->channel));
		*val = tmp * ambadc->t2v_coeff - ambadc->t2v_offset;
		*val2 = 1000;
		mutex_unlock(&ambadc->mtx);
		return IIO_VAL_FRACTIONAL;

	case IIO_CHAN_INFO_RAW:
		if (!test_bit(chan->channel, &ambadc->channels_mask))
			return -EIO;

		mutex_lock(&ambadc->mtx);
		*val = readl_relaxed(ambadc->regbase + ADC_DATA_X_OFFSET(chan->channel));
		*val2 = 0;
		mutex_unlock(&ambadc->mtx);
		return IIO_VAL_INT;

	case IIO_CHAN_INFO_SCALE:
		*val = test_bit(chan->channel, &ambadc->scalers_mask) ? 1800 : 3300;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;

	case IIO_CHAN_INFO_SAMP_FREQ:
		mutex_lock(&ambadc->mtx);
		*val = clk_get_rate(ambadc->clk) / ADC_PERIOD_CYCLE;
		*val /= bitmap_weight(&ambadc->channels_mask, ADC_NUM_CHANNELS);
		*val2 = 0;
		mutex_unlock(&ambadc->mtx);
		return IIO_VAL_INT;

	default:
		return -EINVAL;
	}
}

static int ambarella_adc_write_raw(struct iio_dev *indio_dev,
	struct iio_chan_spec const *chan, int val, int val2, long mask)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	if (mask != IIO_CHAN_INFO_SAMP_FREQ || val <= 0)
		return -EINVAL;

	mutex_lock(&ambadc->mtx);

	val *= ADC_PERIOD_CYCLE;
	val *= bitmap_weight(&ambadc->channels_mask, ADC_NUM_CHANNELS);

	if (val > ADC_MAX_CLOCK)
		val = ADC_MAX_CLOCK;

	clk_set_rate(ambadc->clk, val);

	mutex_unlock(&ambadc->mtx);

	return 0;
}

static int ambarella_adc_read_event_config(struct iio_dev *idev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir)
{
	struct ambarella_adc *ambadc = iio_priv(idev);
	u32 tmp;

	tmp = readl_relaxed(ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));
	return !!(tmp & ADC_INT_THRESHOLD_EN);
}

static int ambarella_adc_write_event_config(struct iio_dev *idev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir, int state)
{
	struct ambarella_adc *ambadc = iio_priv(idev);
	u32 tmp, low, high;

	mutex_lock(&ambadc->mtx);

	tmp = readl_relaxed(ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));

	/* do NOT enable threshold if the threshold values are invalid */
	low = tmp & 0xfff;
	high = (tmp >> 16) & 0xfff;
	if (low >= high) {
		mutex_unlock(&ambadc->mtx);
		return -EPERM;
	}

	if (state)
		tmp |= ADC_INT_THRESHOLD_EN;
	else
		tmp &= ~ADC_INT_THRESHOLD_EN;

	writel_relaxed(tmp, ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));

	mutex_unlock(&ambadc->mtx);

	return 0;
}

static int ambarella_adc_read_event_value(struct iio_dev *idev,
				   const struct iio_chan_spec *chan,
				   enum iio_event_type type,
				   enum iio_event_direction dir,
				   enum iio_event_info info,
				   int *val, int *val2)
{
	struct ambarella_adc *ambadc = iio_priv(idev);
	u32 tmp;
	int rval = IIO_VAL_INT;

	mutex_lock(&ambadc->mtx);

	tmp = readl_relaxed(ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));

	switch (dir) {
	case IIO_EV_DIR_RISING:
		tmp >>= 16;
	case IIO_EV_DIR_FALLING:
		*val = tmp & 0xfff;
		break;
	default:
		rval = -EINVAL;
		break;
	}

	mutex_unlock(&ambadc->mtx);

	return rval;
}

static int ambarella_adc_write_event_value(struct iio_dev *idev,
				    const struct iio_chan_spec *chan,
				    enum iio_event_type type,
				    enum iio_event_direction dir,
				    enum iio_event_info info,
				    int val, int val2)
{
	struct ambarella_adc *ambadc = iio_priv(idev);
	u32 tmp;

	if (val > 0xfff)
		return -EINVAL;

	mutex_lock(&ambadc->mtx);

	tmp = readl_relaxed(ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));

	switch (dir) {
	case IIO_EV_DIR_RISING:
		tmp &= ~(ADC_VAL_HI(0xfff));
		tmp |= ADC_VAL_HI(val);
		ambadc->vol_threshold[chan->channel] = tmp;
		break;
	case IIO_EV_DIR_FALLING:
		tmp &= ~(ADC_VAL_LO(0xfff));
		tmp |= ADC_VAL_LO(val);
		ambadc->vol_threshold[chan->channel] = tmp;
		break;
	default:
		mutex_unlock(&ambadc->mtx);
		return -EINVAL;
	}

	writel_relaxed(tmp, ambadc->regbase + ADC_INT_CTRL_X_OFFSET(chan->channel));

	mutex_unlock(&ambadc->mtx);

	return 0;
}

static int ambarella_adc_validate_trigger(struct iio_dev *indio_dev,
					struct iio_trigger *trig)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	/*
	 * only our own trigger is supported, other triggers provided by
	 * kernel should be able to support also, but need to update the
	 * drivers.
	 */
	return trig != ambadc->trig ? -EINVAL : 0;
}

static const struct iio_info ambarella_adc_iio_info = {
	.driver_module = THIS_MODULE,
	.read_raw = &ambarella_adc_read_raw,
	.write_raw = &ambarella_adc_write_raw,
	.read_event_config = &ambarella_adc_read_event_config,
	.write_event_config = ambarella_adc_write_event_config,
	.read_event_value = &ambarella_adc_read_event_value,
	.write_event_value = &ambarella_adc_write_event_value,
	.validate_trigger = ambarella_adc_validate_trigger,
};

static ssize_t ambarella_adc_read_enable(struct iio_dev *indio_dev,
					  uintptr_t private,
					  const struct iio_chan_spec *chan,
					  char *buf)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", test_bit(chan->channel, &ambadc->channels_mask));
}

static ssize_t ambarella_adc_write_enable(struct iio_dev *indio_dev,
					   uintptr_t private,
					   const struct iio_chan_spec *chan,
					   const char *buf, size_t len)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	bool powerdown;
	int rval;

	if (iio_buffer_enabled(indio_dev))
		return -EBUSY;

	rval = strtobool(buf, &powerdown);
	if (rval)
		return rval;

	mutex_lock(&ambadc->mtx);

	if (test_bit(chan->channel, &ambadc->channels_mask) != powerdown) {
		change_bit(chan->channel, &ambadc->channels_mask);
		ambarella_adc_start(ambadc);
	}

	mutex_unlock(&ambadc->mtx);

	return len;
}

static const struct iio_chan_spec_ext_info ambarella_adc_ext_info[] = {
	{
		.name = "enable",
		.read = ambarella_adc_read_enable,
		.write = ambarella_adc_write_enable,
		.shared = IIO_SEPARATE,
	},
	{ },
};

static const struct iio_event_spec ambarella_adc_events[] = {
	{
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	}, {
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_EITHER,
		.mask_separate = BIT(IIO_EV_INFO_ENABLE),
	},
};

static int ambarella_adc_channel_init(struct iio_dev *indio_dev)
{
	struct ambarella_adc *ambadc = iio_priv(indio_dev);
	struct iio_chan_spec *chan_array, *chan;
	u32 num_channels, idx, bit;

	num_channels = bitmap_weight(&ambadc->channels_mask, 	ADC_NUM_CHANNELS);

	chan_array = devm_kcalloc(ambadc->dev, num_channels,
				sizeof(struct iio_chan_spec), GFP_KERNEL);
	if (chan_array == NULL)
		return -ENOMEM;

	chan = chan_array;
	idx = 0;

	for_each_set_bit(bit, &ambadc->channels_mask, ADC_NUM_CHANNELS) {
		if (bit == ambadc->t2v_channel) {
			chan->type = IIO_TEMP;
			chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
							BIT(IIO_CHAN_INFO_PROCESSED);
			chan->scan_index = -1;
		} else {
			chan->type = IIO_VOLTAGE;
			chan->info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
							BIT(IIO_CHAN_INFO_SCALE);
			chan->scan_index = idx++;
		}
		chan->indexed = 1;
		chan->channel = bit;
		chan->info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ);
		chan->datasheet_name = devm_kasprintf(ambadc->dev, GFP_KERNEL,
							"adc%d", bit);
		chan->ext_info = ambarella_adc_ext_info;
		chan->scan_type.sign = 'u';
		chan->scan_type.realbits = 12;
		chan->scan_type.storagebits = 16;

		chan->event_spec = ambarella_adc_events;
		chan->num_event_specs = ARRAY_SIZE(ambarella_adc_events);

		chan++;
	}

	indio_dev->channels = chan_array;
	indio_dev->num_channels = num_channels;

	return 0;
}

static void ambarella_adc_parse_dt(struct ambarella_adc *ambadc)
{
	struct device_node *np = ambadc->dev->of_node;
	u32 channels_used, scalers_1v8, clk_rate;

	if (of_property_read_u32(np, "amb,channels-used", &channels_used) < 0)
		bitmap_fill(&ambadc->channels_mask, ADC_NUM_CHANNELS);
	else
		ambadc->channels_mask = channels_used;

	if (of_property_read_u32(np, "amb,scaler-1v8", &scalers_1v8) < 0)
		scalers_1v8 = 0;

	/* no scaler for channel 0, so channel 0 range is always 1.8v */
	ambadc->scalers_mask = scalers_1v8 | 0x1;

	if (of_property_read_u32(np, "clock-frequency", &clk_rate) < 0)
		clk_rate = 6000000;

	clk_set_rate(ambadc->clk, clk_rate);

	/* dt properties for t2v channel */
	if (of_property_read_u32(np, "amb,t2v-channel", &ambadc->t2v_channel) < 0
		|| ambadc->t2v_channel >= ADC_NUM_CHANNELS) {
		ambadc->t2v_channel = -1;
	}

	if (ambadc->t2v_channel >= 0) {
		set_bit(ambadc->t2v_channel, &ambadc->channels_mask);

		if (of_property_read_u32(np, "amb,t2v-offset", &ambadc->t2v_offset) < 0)
			ambadc->t2v_offset = AMBARELLA_ADC_T2V_OFFSET;

		if (of_property_read_u32(np, "amb,t2v-coeff", &ambadc->t2v_coeff) < 0)
			ambadc->t2v_coeff = AMBARELLA_ADC_T2V_COEFF;
	}
}

static int ambarella_adc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct ambarella_adc *ambadc;
	struct iio_dev *indio_dev = NULL;
	struct resource	*res;
	int rval;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct ambarella_adc));
	if (!indio_dev) {
		dev_err(&pdev->dev, "failed to allocate iio device\n");
		return -ENOMEM;
	}

	ambadc = iio_priv(indio_dev);
	ambadc->dev = &pdev->dev;
	mutex_init(&ambadc->mtx);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource!\n");
		return -ENXIO;
	}

	ambadc->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (!ambadc->regbase) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return -ENOMEM;
	}

	ambadc->reg_rct = syscon_regmap_lookup_by_phandle(np, "amb,rct-regmap");
	if (IS_ERR(ambadc->reg_rct)) {
		dev_err(&pdev->dev, "no rct regmap!\n");
		return PTR_ERR(ambadc->reg_rct);
	}

	ambadc->irq = platform_get_irq(pdev, 0);
	if (ambadc->irq < 0) {
		dev_err(&pdev->dev, "Can not get irq!\n");
		return -ENXIO;
	}

	ambadc->clk = devm_clk_get(&pdev->dev, "gclk_adc");
	if (IS_ERR(ambadc->clk)) {
		dev_err(&pdev->dev, "failed to get clock, err = %ld\n",
			PTR_ERR(ambadc->clk));
		return PTR_ERR(ambadc->clk);
	}

	ambarella_adc_parse_dt(ambadc);

	platform_set_drvdata(pdev, indio_dev);

	ambarella_adc_key_init(ambadc);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &ambarella_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;

	rval = ambarella_adc_channel_init(indio_dev);
	if (rval < 0)
		return rval;

	rval = iio_triggered_buffer_setup(indio_dev, NULL,
				&ambarella_adc_trigger_handler, NULL);
	if (rval < 0) {
		dev_err(&pdev->dev, "Failed to setup buffer\n");
		return rval;
	}

	ambadc->trig = devm_iio_trigger_alloc(&pdev->dev, "%s-dev%d",
				      indio_dev->name, indio_dev->id);
	if (ambadc->trig == NULL) {
		dev_err(&pdev->dev, "Failed to allocate iio trigger\n");
		return -ENOMEM;
	}

	ambadc->trig->ops = &ambarella_adc_trigger_ops;
	ambadc->trig->dev.parent = &pdev->dev;
	iio_trigger_set_drvdata(ambadc->trig, indio_dev);
	iio_trigger_register(ambadc->trig);
	/* select default trigger */
	indio_dev->trig = iio_trigger_get(ambadc->trig);

	rval = iio_device_register(indio_dev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register iio device %d!\n", rval);
		return -ENXIO;
	}

	rval = devm_request_irq(&pdev->dev, ambadc->irq, ambarella_adc_irq,
				0, dev_name(&pdev->dev), indio_dev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to request irq %d!\n", ambadc->irq);
		return -ENXIO;
	}

	ambarella_adc_power_up(ambadc);
	ambarella_adc_start(ambadc);

	dev_info(&pdev->dev, "%d channels\n", indio_dev->num_channels);

	return 0;
}

static int ambarella_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	if (ambadc->input)
		input_unregister_device(ambadc->input);
	iio_trigger_unregister(ambadc->trig);
	iio_device_unregister(indio_dev);
	ambarella_adc_power_down(ambadc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ambarella_adc_suspend(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	ambadc->clk_rate = clk_get_rate(ambadc->clk);
	ambarella_adc_power_down(ambadc);

	return 0;
}

static int ambarella_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ambarella_adc *ambadc = iio_priv(indio_dev);

	clk_set_rate(ambadc->clk, ambadc->clk_rate);

	ambarella_adc_power_up(ambadc);
	ambarella_adc_start(ambadc);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ambarella_adc_pm_ops,
			 ambarella_adc_suspend,
			 ambarella_adc_resume);

static const struct of_device_id ambarella_adc_match[] = {
	{ .compatible = "ambarella,adc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ambarella_adc_match);

static struct platform_driver ambarella_adc_driver = {
	.probe		= ambarella_adc_probe,
	.remove		= ambarella_adc_remove,
	.driver		= {
		.name	= "ambarella-adc",
		.of_match_table = ambarella_adc_match,
		.pm	= &ambarella_adc_pm_ops,
	},
};

module_platform_driver(ambarella_adc_driver);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("Ambarella ADC driver");
MODULE_LICENSE("GPL v2");
