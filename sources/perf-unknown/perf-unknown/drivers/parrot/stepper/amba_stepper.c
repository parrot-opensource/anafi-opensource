/**
 * Copyright (c) 2017 Parrot Drones
 *
 * @file amba_stepper.c
 * @brief Ambarella Stepper/US driver
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @date 2017-08-10
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h> /* struct platform_driver */
#include <linux/clk.h>             /* clk_get_rate() */

#include "amba_stepper_regs.h"
#include <linux/parrot/amba_stepper.h>

/* Stepper has 4 output pins [SC_n0 to SC_n3] */
#define AMBA_NUM_PWM_STEPPER_CHANNEL_PIN            (4)

/* Maximum number of pwm wave patterns. (the shortest pwm wave takes 2 ticks) */
#define AMBA_NUM_PWM_STEPPER_PATTERN                (64)

typedef struct _amba_pwm_stepper_config_s_ {
	/* stepper freq output */
	u32  samplefreq;

	/* validate data number in pattern array */
	u32 patternsize;

	/* each pattern represents the on/off ticks in single step (lsb first) */
	u8 pattern[AMBA_NUM_PWM_STEPPER_CHANNEL_PIN][AMBA_NUM_PWM_STEPPER_PATTERN];
} amba_pwm_stepper_config_s;

/* This simple driver only writes to Stepper B, which is used
   for sending US pulse */
#define STEPPER_MOTOR_OFFSET STEPPER_MOTOR_B_OFFSET

struct amba_stepper_ctx {
	void __iomem                    *regbase;      /* Stepper base address */
	enum amba_parrot_tx_pulse_cmd_e  status;       /* Enable or Disable */
	enum amba_parrot_tx_pulse_cmd_e  mode;         /* Pulse mode (short range,
	                                                  long range, purge) */
	amba_pwm_stepper_config_s        cfg;
	u32                              nb_pulses[TX_PULSE_COMMANDS_NUM+2];
	u32                              clk_rate;     /* clock base for stepper */
};

static struct amba_stepper_ctx ctx = {
	.status    = TX_PULSE_ENABLE,
	.mode      = TX_PULSE_SHORT_RANGE,
	.cfg = {
		80000,
		2,
		{
			{0},    /* 0 : unused */
			{0, 1}, /* 1 : rectangular signal*/
			{0},    /* 2 : unused */
			{1, 1}, /* 3 : enable signal*/
		}
	},
	.nb_pulses = {
#define AS_NB_PULSES(c, n) n,
		TX_PULSE_COMMANDS(AS_NB_PULSES)
#undef AS_NB_PULSES
	},
};

static int amba_stepper_config(const amba_pwm_stepper_config_s *cfg)
{
	u32 msbpattern;
	u32 lsbpattern;
	int i, j;
	amba_pwm_step_ctrl_reg_u ctrl = {0};


	/* Set pattern */
	for (i = 0; i < AMBA_NUM_PWM_STEPPER_CHANNEL_PIN; i++) {
		msbpattern = 0;
		lsbpattern = 0;
		for (j = 0; j < AMBA_NUM_PWM_STEPPER_PATTERN; j++) {
			if (j < 32)
				msbpattern |= (cfg->pattern[i][j] << (31 - j));
			else
				lsbpattern |= (cfg->pattern[i][j] << (63 - j));
		}
		write_stepper(STEPPER_PATTERN_OFFSET+i*8, msbpattern);
		write_stepper(STEPPER_PATTERN_OFFSET+i*8+4, lsbpattern);
	}

	/* Set ctrl reg */
	ctrl.bits.reset = 1;
	ctrl.bits.clkdivider = ctx.clk_rate/(cfg->samplefreq << 1) - 1;

	ctrl.bits.patternsize = cfg->patternsize - 1;
	ctrl.bits.uselastpinstate = 1;
	ctrl.bits.lastpinstate = 0;
	write_stepper(STEPPER_CTRL_OFFSET, ctrl.data);
	return 0;
}
static int amba_stepper_send_pulses(void)
{
	amba_pwm_stepper_config_s cfg_purge;
	amba_pwm_step_count_reg_u steppercount = {.data = 0};
	steppercount.bits.rewind = 0;
	steppercount.bits.repeatfirst = 0;
	steppercount.bits.repeatlast = 0;
	steppercount.bits.numphase = ctx.nb_pulses[ctx.mode]*2 - 1;


	if (ctx.status != TX_PULSE_ENABLE)
		return 0;

	switch (ctx.mode) {
	case TX_PULSE_SHORT_RANGE:
	case TX_PULSE_LONG_RANGE:
		/* use default pattern */
		amba_stepper_config(&ctx.cfg);
		write_stepper(STEPPER_COUNT_OFFSET, steppercount.data);
		break;
	case TX_PULSE_PURGE:
		/* use purge pattern */
		cfg_purge = ctx.cfg;
		cfg_purge.pattern[1][0] = 1;
		cfg_purge.pattern[1][1] = 1;
		amba_stepper_config(&cfg_purge);
		write_stepper(STEPPER_COUNT_OFFSET, steppercount.data);
		break;
	default:
		break;
	}
	return 0;
}

static int amba_stepper_exec_cmd(struct device *dev, enum amba_parrot_tx_pulse_cmd_e cmd)
{
	switch (cmd) {
	case TX_PULSE_DISABLE:
	case TX_PULSE_ENABLE:
		ctx.status = cmd;
		dev_dbg(dev, "current status: %d", ctx.status);
		break;
	case TX_PULSE_SHORT_RANGE:
	case TX_PULSE_LONG_RANGE:
	case TX_PULSE_PURGE:
		ctx.mode = cmd;
		dev_dbg(dev, "current mode: %d", ctx.mode);
		break;
	case TX_PULSE_SEND:
		return amba_stepper_send_pulses();
	default:
		return -EINVAL;
	}
	return 0;
}

static int amba_stepper_parse_cmd(
	struct device *dev,
	enum amba_parrot_tx_pulse_cmd_e lower_bound,
	enum amba_parrot_tx_pulse_cmd_e upper_bound,
	const char *buf)
{
	enum amba_parrot_tx_pulse_cmd_e cmd;
	int err = -1;

	err = kstrtoint(buf, 10, (int *)&cmd);
	if (err != 0) {
		dev_err(dev, "kstrtoint error\n");
		return err;
	}
	if (cmd < lower_bound
		|| cmd > upper_bound) {
		dev_err(dev, "invalid stepper cmd");
		return -EINVAL;
	}

	return amba_stepper_exec_cmd(dev, cmd);
}

static ssize_t show_tx_pulse_enable(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n", ctx.status);
}

static ssize_t store_tx_pulse_enable(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = amba_stepper_parse_cmd(dev, TX_PULSE_DISABLE, TX_PULSE_ENABLE, buf);

	if (ret)
		return ret;

	return count;
}

static ssize_t show_tx_pulse_info(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	const char *tx_pulse_cmd_str[TX_PULSE_COMMANDS_NUM+2] = {
#define AS_STR(c, n) #c,
		TX_PULSE_COMMANDS(AS_STR)
#undef AS_STR
	};
	ssize_t off = 0;
	ssize_t total = 0;
	int i;

	off = sprintf(buf, "status   : %-21s\n\n", tx_pulse_cmd_str[ctx.status]);
	if (off < 0)
		return -EIO;
	buf += off;
	total += off;

	for (i = TX_PULSE_SHORT_RANGE; i < TX_PULSE_SEND; i++) {
		off = sprintf(buf, "mode (%d) : %-21s : %d %s\n",
			i, tx_pulse_cmd_str[i],  ctx.nb_pulses[i],
			i == ctx.mode ? "<---" : "     ");
		if (off < 0)
			return -EIO;

		buf += off;
		total += off;
	}
	return total;
}

static ssize_t show_tx_pulse_nb(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d %d\n",
		ctx.nb_pulses[TX_PULSE_SHORT_RANGE],
		ctx.nb_pulses[TX_PULSE_LONG_RANGE]);
}

static ssize_t store_tx_pulse_nb(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = 0;
	ret = sscanf(buf, "%d %d",
		&ctx.nb_pulses[TX_PULSE_SHORT_RANGE],
		&ctx.nb_pulses[TX_PULSE_LONG_RANGE]);

	if (ret != 2)
		return -EINVAL;

	return count;
}

static ssize_t show_tx_pulse_mode(
	struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	return sprintf(buf, "%d\n", ctx.mode);
}

static ssize_t store_tx_pulse_mode(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = amba_stepper_parse_cmd(dev, TX_PULSE_SHORT_RANGE, TX_PULSE_PURGE, buf);

	if (ret)
		return ret;

	return count;
}

static ssize_t store_tx_pulse_send(
	struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret = amba_stepper_parse_cmd(dev, TX_PULSE_SEND, TX_PULSE_SEND, buf);

	if (ret)
		return ret;

	return count;
}

static DEVICE_ATTR(tx_pulse_enable, S_IRUSR|S_IWUSR, show_tx_pulse_enable, store_tx_pulse_enable);
static DEVICE_ATTR(tx_pulse_mode, S_IRUSR|S_IWUSR, show_tx_pulse_mode, store_tx_pulse_mode);
static DEVICE_ATTR(tx_pulse_send, S_IWUSR, NULL, store_tx_pulse_send);
static DEVICE_ATTR(tx_pulse_nb, S_IRUSR|S_IWUSR, show_tx_pulse_nb, store_tx_pulse_nb);
static DEVICE_ATTR(tx_pulse_info, S_IRUSR, show_tx_pulse_info, NULL);

static struct attribute *amba_stepper_attributes[] = {
	&dev_attr_tx_pulse_enable.attr,
	&dev_attr_tx_pulse_mode.attr,
	&dev_attr_tx_pulse_send.attr,
	&dev_attr_tx_pulse_nb.attr,
	&dev_attr_tx_pulse_info.attr,
	NULL
};

static struct attribute_group dev_attr_group = {
	.attrs = amba_stepper_attributes,
};

static int amba_stepper_probe(struct platform_device *pdev)
{
	struct resource	*res;
	int ret;

	/* Maps registers */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource !\n");
		return -ENXIO;
	}

	ctx.regbase = devm_ioremap_resource(&pdev->dev, res);
	if (!ctx.regbase) {
		dev_err(&pdev->dev, "Failed to map stepper register !\n");
		return -ENOMEM;
	}
	ret = sysfs_create_group(&pdev->dev.kobj, &dev_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "Failed to create sysfs group !\n");
		return ret ;
	}

	/* Retrieve motor clock base */
	ctx.clk_rate = clk_get_rate(clk_get(NULL, "gclk_motor"));

	dev_info(&pdev->dev, "Stepper init.\n");
	return 0;
}

static int amba_stepper_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &dev_attr_group);
	return 0;
}

static  struct of_device_id amba_stepper_local_dt_ids[] = {
	{.compatible = "parrot,stepper", },
	{},
};
MODULE_DEVICE_TABLE(of, amba_stepper_local_dt_ids);

static struct platform_driver amba_stepper_device = {
	.driver	= {
		.name	= "parrot-stepper",
		.owner	= THIS_MODULE,
		.of_match_table	= amba_stepper_local_dt_ids,
	},
	.probe		= amba_stepper_probe,
	.remove		= amba_stepper_remove,
};

/**
 * amba_stepper_init() -  device driver registration
 *
 */
static __init int amba_stepper_init(void)
{
	int err = 0;

	err = platform_driver_register(&amba_stepper_device);
	if (err) {
		pr_err("amba_stepper: platform_driver_register failed !\n");
	}
	return err;
}
module_init(amba_stepper_init);

/**
 * amba_stepper_exit() - device driver removal
 *
 */
static __exit void amba_stepper_exit(void)
{

	platform_driver_unregister(&amba_stepper_device);
}
module_exit(amba_stepper_exit);

MODULE_AUTHOR("Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com");
MODULE_DESCRIPTION("Ambarella Stepper/US driver");
MODULE_LICENSE("GPL v2");
