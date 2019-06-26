/**
 * Copyright (c) 2017 Parrot SA
 *
 * @file amba_a9s_adc_local.c
 * @brief Ambarella A9S ADC IIO local backend (Linux stand-alone)
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2017-02-06
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>             /* clk_get_rate() */
#include <linux/delay.h>           /* udelay() */
#include <linux/io.h>              /* amba_{set|clr}bits (__raw_{read|write}) */
#include <linux/interrupt.h>       /* IRQ_HANDLED, *_irq() */
#include <linux/irq.h>             /* irq_to_desc() */
#include <linux/mfd/syscon.h>      /* syscon_regmap_lookup_by_phandle() (RCT reg mapping) */
#include <linux/of.h>              /* struct device_node */
#include <linux/platform_device.h> /* struct platform_driver */
#include <linux/regmap.h>          /* regmap_write() (RCT reg mapping) */
#include <linux/sched.h>           /* wake_up_interruptible() */
#include <linux/slab.h>            /* kmalloc() */
#include <linux/wait.h>            /* wake_up_interruptible() */
#include <plat/adc.h>              /* ADC_DATA_REG, ADC_CONTROL_REG, etc */
#include <plat/rct.h>              /* RCT_REG (<= ADC16_CTRL_REG) */
#include <linux/iio/buffer.h>      /* iio_push_to_buffers_n() */

#include "amba_a9s_adc.h"
#include "amba_a9s_adc_cmd.h"
#include "amba_a9s_adc_cmd_interface.h"

/* Use full fifo size */
#define AMBA_ADC_FIFO_DEPTH 1024

/* Interrupt when 3/4 fifo is full */
#define AMBA_ADC_FIFO_TH ((AMBA_ADC_FIFO_DEPTH >> 2) * 3)

static const char *adc_cmd_str[ADC_COMMANDS_NUM+2] = {
#define AS_STR(c) #c,
	ADC_COMMANDS(AS_STR)
#undef AS_STR
};

#define write_adc(offset, value)  writel_relaxed((value), (st->regbase + offset))
#define read_adc(offset)  readl_relaxed((st->regbase + offset))

#define setbitl(offset, value)   (write_adc(offset, (value) | (read_adc((offset)))))
#define clrbitl(offset, value)   (write_adc(offset, (~(value)) & read_adc((offset))))

/* Pointer to ADC private state allocated and provided by amba_a9s_adc.c */
struct amba_a9s_adc_state *st;

static void amba_a9s_adc_local_start(void)
{
	setbitl(ADC_CONTROL_OFFSET, ADC_CONTROL_START);
}

static void amba_a9s_adc_local_stop(void)
{
	clrbitl(ADC_CONTROL_OFFSET, ADC_CONTROL_START);
}

static void amba_a9s_adc_local_setup_adc(bool cold_boot)
{
	/* Set continuous sampling  & Enable ADC */
	write_adc(ADC_CONTROL_OFFSET, ADC_CONTROL_MODE | ADC_CONTROL_ENABLE);

	/* Wait 3us for ADC stabilization, see 19.3.1 in HW programming guide */
	udelay(3);

	/* Set acquisition rate */
	write_adc(ADC_SLOT_PERIOD_OFFSET, st->pdata.period-1);

	/* Activate 3 timeslots (counting start at 0)*/
	write_adc(ADC_SLOT_NUM_OFFSET, 2);

	/* Pre-allocate time division. Each high freq channel
	 * is acquired at each slot period.
	 * The other lowfreq channels are acquired every 3 periods. */
	write_adc(ADC_SLOT_CTRL_0_OFFSET,
		st->pdata.highfreq_channels | 1 << st->pdata.lowfreq_channels[0]);
	write_adc(ADC_SLOT_CTRL_1_OFFSET,
		st->pdata.highfreq_channels | 1 << st->pdata.lowfreq_channels[1]);
	write_adc(ADC_SLOT_CTRL_2_OFFSET,
		st->pdata.highfreq_channels | 1 << st->pdata.lowfreq_channels[2]);

	if (cold_boot) {
		/* Power-up ADC */
		regmap_write(st->rctregbase, ADC16_CTRL_OFFSET, 0x0);

		/* Wait 4ms for ADC power stabilization. See bug 187819 */
		mdelay(4);
	}

	/* Initiate a first ADC Start/Stop, see bug 195778#c674422 */
	amba_a9s_adc_local_start();
	mdelay(1);
	amba_a9s_adc_local_stop();
}

static int amba_a9s_adc_local_getdata(
	enum amba_parrot_adc_channel_e channel,
	u32 *data)
{

	/* If no capture is in progress, ADC is not running and must
	 * be started specially for this request */
	if (st->capture_enable == 0) {
		amba_a9s_adc_local_start();

		/* Check at least one conversion occured */
		while (read_adc(ADC_STATUS_OFFSET) != 0x1)
			;

		amba_a9s_adc_local_stop();
	}

	*data = read_adc(ADC_DATA_X_OFFSET(channel));
	return 0;
}

static irqreturn_t amba_a9s_adc_local_irq_worker(int irq, void *dev_id)
{
	struct amba_a9s_adc_state *st;
	st = dev_id;

	if (st->count >= st->nb_samples) {
		iio_push_to_buffers_n(st->indio_dev,
			&(st->buffer[st->idx]),
			st->nb_samples - st->idx);
		if (st->idx >= 1)
			iio_push_to_buffers_n(st->indio_dev, st->buffer, st->idx);
		st->idx = 0;
		st->count = 0;
	}

	return IRQ_HANDLED;
}

static irqreturn_t amba_a9s_adc_local_irq_handler(int irq, void *dev_id)
{
	int i;
	u32 reg, mask, fifocounter;
	struct amba_a9s_adc_state *st;
	struct platform_device *pdev;
	irqreturn_t ret = IRQ_HANDLED;

	st = dev_id;
	pdev = st->pdev;

	/* Check ADC error, should not happen */
	reg = read_adc(ADC_CTRL_INTR_TABLE_OFFSET);
	if (reg & 0x1) {

		dev_err_ratelimited(&pdev->dev, "Critical ADC error or underflow. Resetting.");

		/* Clear error */
		write_adc(ADC_CTRL_INTR_TABLE_OFFSET, 0x1);

		/* Stop ADC */
		amba_a9s_adc_local_stop();

		/* Disable ADC */
		clrbitl(ADC_CONTROL_OFFSET, ADC_CONTROL_ENABLE);

		/* Nothing else to do than soft-reset ADC */
		write_adc(ADC_CONTROL_OFFSET, ADC_CONTROL_RESET);

		/* Re-do setup */
		amba_a9s_adc_local_setup_adc(false);

		return IRQ_HANDLED;
	}

	/* We are not interested in event counter interrupts */
	if (reg & 0x2)
		write_adc(ADC_CTRL_INTR_TABLE_OFFSET, 0x2);

	/* Check FIFOs interrupts */
	reg = read_adc(ADC_FIFO_INTR_TABLE_OFFSET);
	if (reg) {
		/* Check each FIFO status */
		for (i = 0, mask = 0x1; i < ADC_FIFO_NUMBER; i++, mask <<= 1) {

			/* Clean abort when either : FIFOi is in overflow or
			 * capture is not set up */
			if (reg & mask << 8 || st->capture_enable == 0) {

				/* [10:0] FIFO data counter */
				fifocounter = read_adc(ADC_FIFO_STATUS_X_OFFSET(i)) & 0x07ff;

				/* Empty fifo */
				while (fifocounter--)
					read_adc(ADC_FIFO_DATA_X_OFFSET(i));
			}
			/* FIFOi is in underflow. Always triggered above,
			 * in the first ADC error check, but I add it here
			 * just in case*/
			else if(reg & mask << 4) {
				dev_err_ratelimited(&pdev->dev, "ADC underflow");
				udelay(50);
			}
			/* If FIFOi has reached its threshold */
			else if (reg  & mask) {
				/* Get amount of samples to pop */
				/* [10:0] FIFO data counter */
				fifocounter = read_adc(ADC_FIFO_STATUS_X_OFFSET(i)) & 0x07ff;

#if defined(CONFIG_IIO_AMBA_A9S_ADC_ENABLE_FIFO_BURST_READ)
				while (fifocounter >= 32) {
					/* Burst-read 32 FIFO entries */

/* Disable memcpy_fromio() implementation because it creates SPI storm interrupts.
   readsl() is not optimized at all (unlike memcpy_fromio()), but it doesn't crash our system.
   See bug 0208715
   See https://smet.parrot.biz/projects/anafi/wiki/AnafiADCPerf
 */
#if 0
					memcpy_fromio(&(st->buffer[st->idx]), (st->regbase + ADC_FIFO_DATA_X_OFFSET(i)),  32 * sizeof(struct amba_parrot_adc_capture_sample_s));
#else
					readsl((st->regbase + ADC_FIFO_DATA_X_OFFSET(i)), &(st->buffer[st->idx]), 32 * sizeof(struct amba_parrot_adc_capture_sample_s)/sizeof(u32));
#endif

					/* This is a ring buffer */
					if (st->idx + 32 >= st->nb_samples)
						st->idx = 0;
					else
						st->idx += 32;

					/* Update global count of the current capture */
					st->count += 32;

					if (st->idx == 0)
						/* Leave the rest of FIFO samples for next time, and
						   let iio push happen. Otherwise, we might reloop
						   and loose early samples */
						break;

					fifocounter -= 32;
				}
#else
				while (fifocounter) {
					st->buffer[st->idx].channel = read_adc(ADC_FIFO_DATA_X_OFFSET(i));

					/* This is a ring buffer */
					if ((st->idx + 1) >= st->nb_samples)
						st->idx = 0;
					else
						st->idx += 1;

					/* Update global count of the current capture */
					st->count +=1;

					if (st->idx == 0)
						/* Leave the rest of FIFO samples for next time, and
						   let iio push happen. Otherwise, we might reloop
						   and loose early samples */
						break;

					fifocounter--;
				}
#endif
				if (st->count >= st->nb_samples)
					ret = IRQ_WAKE_THREAD;
			}

		}
		/* Clean FIFOs interrupts */
		write_adc(ADC_FIFO_INTR_TABLE_OFFSET, reg);
	}

	/* Check/Clean Data interrupts */
	reg = read_adc(ADC_DATA_INTR_TABLE_OFFSET);
	if (reg)
		write_adc(ADC_DATA_INTR_TABLE_OFFSET, reg);

	return ret;
}

int amba_a9s_adc_local_enable_capture(u32 active_channels, u32 nb_samples)
{
	int i, mask;
	u32 fifoctrl_value = 0;
	u32 fifoparam_err = 0;
	struct irq_desc *desc;
	struct platform_device *pdev = st->pdev;
	dev_dbg(&pdev->dev,
		"%s: enabling capture for channels 0x%08X nb samples: %u",
		adc_cmd_str[ADC_ENABLE_CAPTURE], active_channels, nb_samples);


	if (nb_samples > AMBA_A9S_ADC_BUFFER_MAX_SAMPLES) {
		dev_err(&pdev->dev,
		"%s: error: max samples is %u (requested: %u)",
		adc_cmd_str[ADC_ENABLE_CAPTURE],
		AMBA_A9S_ADC_BUFFER_MAX_SAMPLES,
		nb_samples);
		return -EINVAL;
	}

	/* Configure FIFO to capture the requested channel in active_channels */
	for (i = 0, mask = 0x1; i < AMBA_NUM_ADC_CHANNEL; i++, mask <<= 1) {
		if (active_channels & mask) {
			dev_dbg(&pdev->dev, "%s: configuring FIFO 0 for channel %d (size: %u)",
							adc_cmd_str[ADC_ENABLE_CAPTURE],
							i,
							AMBA_ADC_FIFO_DEPTH);

			fifoctrl_value =
				1 << 31                    /* [31] 1 = Enable FIFO overflow interrupt */
				|1 << 30                   /* [30] 1 = Enable FIFO underflow interrupt */
				|AMBA_ADC_FIFO_TH << 16    /* [26:15 XXX 26:16] Trigger interrupt when FIFO depth is over the threshold */
				| (i << 12)                /* [14:12] Channel number associated with this FIFO */
				| AMBA_ADC_FIFO_DEPTH;     /* [10:0] FIFO depth for 12-bit ADC samples */

			write_adc(ADC_FIFO_CTRL_X_OFFSET(0), fifoctrl_value);

			break; /* FIXME: support more than one channel */
		}
	}
	fifoparam_err = read_adc(ADC_ERR_STATUS_OFFSET);
	if (fifoparam_err & 0x2) {
		dev_err(&pdev->dev, "%s: FIFO parameters error bit set.",
			adc_cmd_str[ADC_ENABLE_CAPTURE]);
		return -EINVAL;
	}

	/* Init state */
	st->capture_enable = 1;
	st->idx = 0;
	st->count = 0;
	st->nb_samples = nb_samples;
	memset(st->buffer,
		0,
		sizeof(
			struct amba_parrot_adc_capture_sample_s)*
			AMBA_A9S_ADC_BUFFER_MAX_SAMPLES
		);

	/* Start Capture */
	desc = irq_to_desc(st->irq);
	if (desc && desc->depth > 0)
		enable_irq(st->irq);

	write_adc(ADC_FIFO_CTRL_OFFSET, ADC_FIFO_CLEAR);
	amba_a9s_adc_local_start();

	return 0;
}

int amba_a9s_adc_local_disable_capture(void)
{
	struct platform_device *pdev = st->pdev;
	dev_dbg(&pdev->dev,
		"%s: count: %u",
		adc_cmd_str[ADC_DISABLE_CAPTURE],
		st->count);

	amba_a9s_adc_local_stop();
	disable_irq(st->irq);
	st->capture_enable = 0;

	return 0;
}

int amba_a9s_adc_cmd(void *data, int len)
{
	struct amba_parrot_adc_message_s *msg;
	enum amba_parrot_adc_channel_e channel; /* ADC_GET_DATA args */
	u32 active_channels, nb_samples; /* ADC_ENABLE_CAPTURE args */
	struct platform_device *pdev = NULL;
	int err = 0;
	u32 reg = 0;

	/* Check ADC local backend is probed */
	if (st == NULL || (pdev = st->pdev) == NULL) {
		pr_err("amba_parrot_adc device not initialized !\n");
		err = 1;
		goto end;
	}

	/* Sanitize */
	if (data == NULL) {
		dev_err(&pdev->dev, "bad parameter");
		err = 1;
		goto end;
	}
	/* Parse message */
	msg = (struct amba_parrot_adc_message_s *)data;
	switch (msg->cmd) {
	case ADC_GET_DATA:
		channel = *(enum amba_parrot_adc_channel_e *)msg->buffer;
		if (channel >= ARRAY_SIZE(st->adc_val)) {
			dev_err(&pdev->dev, "bad channel: %d", channel);
			err = 1;
			goto end;
		}
		if (amba_a9s_adc_local_getdata(channel, &reg) == 0)
			st->adc_val[channel] = reg;

		dev_dbg(&pdev->dev, "%s: channel: %d value: %d",
			adc_cmd_str[msg->cmd],
			channel, st->adc_val[channel]);

		st->read_raw_done = true;
		wake_up_interruptible(&st->wq_data_available);
		break;

	case ADC_ENABLE_CAPTURE:
		if (msg->length != (sizeof(u32)*2)) {
			dev_err(&pdev->dev,
				"%s:command is malformed.",
				adc_cmd_str[msg->cmd]);
			err = 1;
			goto end;
		}
		active_channels = ((u32 *)msg->buffer)[0];
		nb_samples = ((u32 *)msg->buffer)[1];

		/* Start FIFO capture of active_channels, when nb_samples
		 * are captured, the buffer will be pushed to IIO */
		amba_a9s_adc_local_enable_capture(active_channels, nb_samples);
		break;

	case ADC_DISABLE_CAPTURE:
		/* Stop FIFO acquisition */
		amba_a9s_adc_local_disable_capture();
		break;

	default:
		dev_info(&pdev->dev, "%d: invalid command.", msg->cmd);
		break;
	}

end:
	return err;
}
EXPORT_SYMBOL(amba_a9s_adc_cmd);

static int amba_a9s_adc_local_parse_dt(struct platform_device *pdev)
{
	int ret = 1;
	int err = 0;
	struct device_node *node;

	/* Retrieve DT node */
	node = pdev->dev.of_node;

	/* Read ADC period acquisition in nb of CLK cycles */
	ret = of_property_read_u32(node, "period", &st->pdata.period);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse period property");
		err = 1;
	}

	/* Read high freq channels mask */
	ret = of_property_read_u32(node,
		"highfreq_channels",
		&st->pdata.highfreq_channels);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse highfreq_channels property");
		err = 1;
	}

	/* Read low freq channels array */
	ret = of_property_read_u32_array(node,
		"lowfreq_channels",
		st->pdata.lowfreq_channels, 3);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse lowfreq_channels property");
		err = 1;
	}

	if (err)
		return -EINVAL;

	return 0;
}

static int amba_a9s_adc_local_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource	*res;
	int ret;

	/* Retrieve period & channels config */
	ret = amba_a9s_adc_local_parse_dt(pdev);
	if (ret != 0)
		return ret;

	/* Get ADC registers access */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "No mem resource !\n");
		return -ENXIO;
	}

	st->regbase = devm_ioremap_resource(&pdev->dev, res);
	if (!st->regbase) {
		dev_err(&pdev->dev, "Failed to map adc register !\n");
		return -ENOMEM;
	}

	/* Get RCT registers access (power control) */
	st->rctregbase = syscon_regmap_lookup_by_phandle(np, "amb,rct-regmap");
	if (IS_ERR(st->rctregbase)) {
		dev_err(&pdev->dev, "Failed to map rct register !\n");
		return PTR_ERR(st->rctregbase);
	}

	/* Pre-allocate capture buffer */
	st->buffer = kmalloc(sizeof(struct amba_parrot_adc_capture_sample_s)*AMBA_A9S_ADC_BUFFER_MAX_SAMPLES, GFP_KERNEL);
	if (st->buffer == NULL)
		return -ENOMEM;


	/* Retrieve IRQ number */
	st->irq = platform_get_irq(pdev, 0);
	if (st->irq < 0) {
		dev_err(&pdev->dev, "cannot get irq!");
		return -ENXIO;
	}

	/* Install ISR */
	st->pdev = pdev;
	ret = devm_request_threaded_irq(&pdev->dev,
		st->irq,
		amba_a9s_adc_local_irq_handler,
		amba_a9s_adc_local_irq_worker,
		IRQF_TRIGGER_HIGH,
		dev_name(&pdev->dev),
		st);
	if (ret < 0) {
		dev_err(&pdev->dev, "cannot request irq %d!",
		st->irq);
		return -ENXIO;
	}

	/* Soft reset (write-only) */
	write_adc(ADC_CONTROL_OFFSET, ADC_CONTROL_RESET);

	/* Setup ADC */
	amba_a9s_adc_local_setup_adc(true);

	/* Use 1.5Mhz ADC Clock: that allows acquisition of each slot at 37.5kHz
	   (period: 40 ticks) */
	clk_set_rate(clk_get(NULL, "gclk_adc"), 1500000);

	dev_info(&pdev->dev, "AmbaParrot ADC driver init");
	return 0;
}

static int amba_a9s_adc_local_remove(struct platform_device *pdev)
{
	amba_a9s_adc_local_stop();

	/* Disable ADC */
	clrbitl(ADC_CONTROL_OFFSET, ADC_CONTROL_ENABLE);

	/* Power down ADC */
	regmap_write(st->rctregbase, ADC16_CTRL_OFFSET, ADC_CTRL_POWERDOWN);

	kfree(st->buffer);
	st->buffer = NULL;

	return 0;
}

static const struct of_device_id amba_a9s_adc_local_dt_ids[] = {
	{.compatible = "parrot,adc", },
	{},
};
MODULE_DEVICE_TABLE(of, amba_a9s_adc_local_dt_ids);

static struct platform_driver amba_a9s_adc_local_driver = {
	.driver	= {
		.name	= "parrot-adc",
		.owner	= THIS_MODULE,
		.of_match_table	= amba_a9s_adc_local_dt_ids,
	},
	.probe		= amba_a9s_adc_local_probe,
	.remove		= amba_a9s_adc_local_remove,
};

int amba_a9s_adc_cmd_init(void *priv)
{
	/* Store ADC private state for later use
	 * in amba_a9s_adc_cmd() implementation */
	st = priv;

	return platform_driver_register(&amba_a9s_adc_local_driver);
}
EXPORT_SYMBOL(amba_a9s_adc_cmd_init);

void amba_a9s_adc_cmd_exit(void)
{
	platform_driver_unregister(&amba_a9s_adc_local_driver);
}
EXPORT_SYMBOL(amba_a9s_adc_cmd_exit);
