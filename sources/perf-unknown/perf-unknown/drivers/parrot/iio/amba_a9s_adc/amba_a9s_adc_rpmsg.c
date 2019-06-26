/**
 * Copyright (c) 2016 Parrot SA
 *
 * @file amba_a9s_adc_rpmsg.c
 * @brief Ambarella A9S ADC IIO driver rpmsg backend
 * @author Jean-Louis Thekekara <jeanlouis.thekekara@parrot.com>
 * @version 0.1
 * @date 2016-05-09
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>
#include <linux/sched.h>        /* wake_up_interruptible() */
#include <linux/wait.h>         /* wake_up_interruptible() */
#include <linux/iio/iio.h>
#include <linux/iio/kfifo_buf.h>
#include <mach/memory.h>        /* NOLINUX_MEM_V_START */

#include "amba_a9s_adc.h"
#include "amba_a9s_adc_cmd.h"
#include "amba_a9s_adc_cmd_interface.h"

static struct rpmsg_channel *g_rpdev;
static void *g_priv;
static const char *adc_cmd_str[ADC_COMMANDS_NUM+2] = {
#define AS_STR(c) #c,
	ADC_COMMANDS(AS_STR)
#undef AS_STR
};

/* ThreadX memory start is mapped (at NOLINUX_MEM_V_START+1MB) */
/* reachable ThreadX memory size is thus reduced by 1 MB */
/* Threadx memory mapping is called "PPM2" */
/*  PPM2 = 0x00100000[0xa0100000],0x37f00000 16 */
#define THREADX_MEM_START (NOLINUX_MEM_V_START + 0x00100000) /*Ex: 0xa0100000 */
#define THREADX_MEM_SIZE  (CONFIG_AMBALINK_SHMADDR- NOLINUX_MEM_V_START - 0x0010000)
#define THREADX_MEM_END   (THREADX_MEM_START + THREADX_MEM_SIZE)

static void amba_a9s_adc_rpmsg_cb(struct rpmsg_channel *rpdev, void *data, int len,
			void *priv, u32 src)
{
	struct amba_parrot_adc_message_s *msg;
	struct amba_a9s_adc_state *st;

	/* ADC_PUSH_DATA args */
	enum amba_parrot_adc_channel_e *channel;
	u32 *value;

	/* ADC_DATA_AVAIBLE args & ack */
	struct amba_parrot_adc_capture_sample_s **capture_buffer;
	u32 *size;
	u32 *index;
	struct amba_parrot_adc_message_s ack = {
		.cmd = ADC_DATA_ACK,
		.length = 0,
	};

	/* Sanity */
	if (data == NULL || priv == NULL) {
		dev_err(&rpdev->dev, "invalid parameters");
		return;
	}

	/* Retrieve ADC state stored during RPMSG driver probe */
	st = priv;

	/* Parse message */
	msg = (struct amba_parrot_adc_message_s *)data;
	switch (msg->cmd) {
	case ADC_PUSH_DATA:
		/* Sanity check */
		if (msg->length != sizeof(enum amba_parrot_adc_channel_e)+sizeof(u32)) {
			dev_err(&rpdev->dev, "%s: malformed command", adc_cmd_str[msg->cmd]);
			return;
		}

		/* Sanity check on channel number */
		channel = (enum amba_parrot_adc_channel_e *)msg->buffer;
		if (*channel < 0 || *channel >= ARRAY_SIZE(st->adc_val)) {
			dev_err(&rpdev->dev, "%s: invalid channel %d",
							adc_cmd_str[msg->cmd],
							*channel);
			return;
		}

		/* Store channel value in ADC state
		 * and notify consumer */
		value = (u32 *)(msg->buffer+sizeof(enum amba_parrot_adc_channel_e));
		st->adc_val[*channel] = *value;
		dev_dbg(&rpdev->dev, "%s: channel: %d value: %u",
						adc_cmd_str[msg->cmd],
						*channel,
						*value);
		st->read_raw_done = true;
		wake_up_interruptible(&st->wq_data_available);
		break;
	case ADC_DATA_AVAILABLE:
		/* Sanity check */
		if (msg->length != sizeof(u16 *)+sizeof(u32)+sizeof(u32)) {
			dev_err(&rpdev->dev, "%s: malformed command", adc_cmd_str[msg->cmd]);
			return;
		}
		capture_buffer = (struct amba_parrot_adc_capture_sample_s **)(msg->buffer);


		/* Translate ThreadX address received. Linux maps ThreadX memory space (ppm2) at NOLINUX_MEM_V_START */
		*capture_buffer = (struct amba_parrot_adc_capture_sample_s *)ambarella_phys_to_virt((u32)*capture_buffer);

		size = (u32 *)(msg->buffer+sizeof(struct amba_parrot_adc_capture_sample_s *));
		if (*size > AMBA_A9S_ADC_BUFFER_MAX_SAMPLES) {
			dev_err(&rpdev->dev, "invalid size received: %u", *size);
			goto end_adc_data_available;
		}

		index = (u32 *)(msg->buffer+sizeof(struct amba_parrot_adc_capture_sample_s *)+sizeof(u32));
		if(*index > *size) {
			dev_err(&rpdev->dev, "invalid index received: %u", *index);
			goto end_adc_data_available;
		}

		dev_dbg(&rpdev->dev, "capture available at 0x%p, size:%u, idx: %u", *capture_buffer, *size, *index);

		/* Push directly the preformated shared buffer */
		/* capture_buffer is a ring buffer, push it in two steps to keep samples order */
		if ((u32)&(*capture_buffer)[*index] >= THREADX_MEM_END ||
				(u32)&(*capture_buffer)[*index] < THREADX_MEM_START ||
				*size > AMBA_A9S_ADC_BUFFER_MAX_SAMPLES ||
				*index > *size) {
			dev_err(&rpdev->dev, "invalid 1st iio_push_to_buffers_n: 0x%p, size:%u", &(*capture_buffer)[*index],((*size)-(*index)));
			goto end_adc_data_available;
		}
		iio_push_to_buffers_n(st->indio_dev, &(*capture_buffer)[*index], (*size)-(*index));

		/* Let's check again, have seen corrupted capture_buffer base address after the first iio_push_to_buffers_n() call */
		if ((u32)*capture_buffer >= THREADX_MEM_END ||
				(u32)*capture_buffer < THREADX_MEM_START ||
				*size > AMBA_A9S_ADC_BUFFER_MAX_SAMPLES ||
				*index > *size) {
			dev_err(&rpdev->dev, "invalid 2nd iio_push_to_buffers_n: 0x%p, size:%u", *capture_buffer, *index);
			goto end_adc_data_available;
		}
		iio_push_to_buffers_n(st->indio_dev, *capture_buffer, (*index));

end_adc_data_available:
		/* Send Data ACK */
		amba_a9s_adc_cmd(&ack, sizeof(ack));
		break;
	default:
		dev_err(&rpdev->dev, "unknown command: %u\n", msg->cmd);
		break;
	}
}

static int amba_a9s_adc_rpmsg_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	/* Backup rpdev for later use, to initiate comm with ThreadX task */
	g_rpdev = rpdev;

	/* Store ADC private state for later use by rpmsg callback */
	rpdev->ept->priv = g_priv;

	/* No one should use this, but ept->priv instead */
	g_priv = NULL;

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void amba_a9s_adc_rpmsg_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id amba_a9s_adc_rpmsg_id_table[] = {
	{ .name	= ADC_RPMSG_CHANNEL, },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, amba_a9s_adc_rpmsg_id_table);

static struct rpmsg_driver amba_a9s_adc_rpmsg_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= amba_a9s_adc_rpmsg_id_table,
	.probe		= amba_a9s_adc_rpmsg_probe,
	.callback	= amba_a9s_adc_rpmsg_cb,
	.remove		= amba_a9s_adc_rpmsg_remove,
};

/**
 * Public API used by amba_a9s_adc
 */
int amba_a9s_adc_cmd_init(void *priv)
{
	g_priv = priv;
	return register_rpmsg_driver(&amba_a9s_adc_rpmsg_driver);
}
EXPORT_SYMBOL(amba_a9s_adc_cmd_init);

void amba_a9s_adc_cmd_exit(void)
{
	unregister_rpmsg_driver(&amba_a9s_adc_rpmsg_driver);
	return;
}
EXPORT_SYMBOL(amba_a9s_adc_cmd_exit);

int amba_a9s_adc_cmd(void *data, int len)
{
	/* Delegate work to ThreadX driver */
	return	rpmsg_send(g_rpdev, data, len);
}
EXPORT_SYMBOL(amba_a9s_adc_cmd);
