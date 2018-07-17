/*
 * drivers/parrot/i2c/i2c-threadx.c
 *
 * Copyright (C) 2017 Parrot SA
 * Author: Alexandre Dilly <alexandre.dilly@parrot.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/wait.h>
#include <linux/rpmsg.h>
#include <linux/psi2c.h>

struct threadx_i2c_dev {
	struct device			*dev;
	struct i2c_adapter		 adapter;

	wait_queue_head_t		 msg_queue;
	struct mutex			 lock;

	unsigned char			*msg_buffer;
	size_t				 msg_buffer_size;

	enum psi2c_state		 state;
};

/* Use Parrot Shared I2C bus channel for driver */
#define THREADX_I2C_CHANNEL PSI2C_RPMSG_CHANNEL

#define THREADX_I2C_TIMEOUT (5 * HZ)

/* rpmsg device for ThreadX I2C channel */
static struct threadx_i2c_dev *i2c_devs[PSI2C_CHANNEL_COUNT];
static struct rpmsg_device *rp_device;

static unsigned char *threadx_i2c_map_msg_buffer(struct psi2c_msg_buffer *buf)
{
	/* HACK: currently, the memory used on ThreadX is already mapped in
	 * kernel space and we can access to by simply converting physical
	 * address to virtual address in kernel memory space.
	 */
	return phys_to_virt((phys_addr_t) psi2c_to_ptr(buf->address));
}

static void threadx_i2c_unmap_msg_buffer(struct threadx_i2c_dev *i2c_dev)
{
}

static int threadx_i2c_rpmsg_cb(struct rpmsg_device *rpdev, void *data, int len,
				void *priv, u32 src)
{
	struct threadx_i2c_dev *i2c_dev;
	struct psi2c_rpmsg *msg = data;

	/* Check rpmsg */
	if (!data || len != sizeof(*msg)) {
		dev_warn(&rpdev->dev, "bad rpmsg received\n");
		return -EINVAL;
	}

	/* Get device from channel */
	if (msg->channel >= PSI2C_CHANNEL_COUNT) {
		dev_warn(&rpdev->dev, "bad channel\n");
		return -ENODEV;
	}
	i2c_dev = i2c_devs[msg->channel];
	if (!i2c_dev) {
		dev_warn(&rpdev->dev, "I2C channel %d is not initialized\n",
			 msg->channel);
		return -EAGAIN;
	}

	/* Parse rpmsg */
	switch (msg->cmd) {
	case PSI2C_CMD_GET_MSG_BUFFER:
		/* Set shared message buffer values */
		i2c_dev->msg_buffer = threadx_i2c_map_msg_buffer(
							      &msg->msg_buffer);
		i2c_dev->msg_buffer_size = msg->msg_buffer.size;
		wake_up(&i2c_dev->msg_queue);
		break;
	case PSI2C_CMD_TRANSFER:
		/* Transfer is done on ThreadX */
		i2c_dev->state = msg->state;
		wake_up(&i2c_dev->msg_queue);
		break;
	default:
		dev_warn(&rpdev->dev, "bad command\n");
		return -1;
	}

	return 0;
}

static int threadx_i2c_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rpmsg_channel_info chinfo;

	rp_device = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.name[sizeof(chinfo.name)-1] = 0;
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return 0;
}

static void threadx_i2c_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id threadx_i2c_rpmsg_id_table[] = {
	{ .name	= THREADX_I2C_CHANNEL, },
};
MODULE_DEVICE_TABLE(rpmsg, threadx_i2c_rpmsg_id_table);

static struct rpmsg_driver threadx_i2c_rpmsg_driver = {
	.drv	= {
		.name	= KBUILD_MODNAME,
		.owner	= THIS_MODULE,
	},
	.id_table	= threadx_i2c_rpmsg_id_table,
	.probe		= threadx_i2c_rpmsg_probe,
	.callback	= threadx_i2c_rpmsg_cb,
	.remove		= threadx_i2c_rpmsg_remove,
};

static int threadx_i2c_get_msg_buffer(struct threadx_i2c_dev *i2c_dev)
{
	struct psi2c_rpmsg msg = {
		.cmd = PSI2C_CMD_GET_MSG_BUFFER,
		.channel = i2c_dev->adapter.nr,
	};
	int ret;

	/* Send request for shared message buffer */
	rpmsg_send(rp_device->ept, &msg, sizeof(msg));

	/* Wait for completion */
	ret = wait_event_interruptible_timeout(i2c_dev->msg_queue,
					       i2c_dev->msg_buffer,
					       THREADX_I2C_TIMEOUT);
	if (ret <= 0)
		return !ret ? -EBUSY : ret;

	/* No address / size of buffer received */
	if (!i2c_dev->msg_buffer || !i2c_dev->msg_buffer_size)
		return -ENOMEM;

	return 0;
}

static int threadx_i2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs,
			    int num)
{
	struct threadx_i2c_dev *i2c_dev = i2c_get_adapdata(adap);
	struct psi2c_rpmsg msg;
	unsigned char *rx_buf;
	u16 tx_size, rx_size;
	int ret;

	/* Parrot Shared I2C rpmsg is not yet available */
	if (!rp_device)
		return -ENODEV;

	/* Allocate shared message buffer for this I2C channel */
	if (!i2c_dev->msg_buffer) {
		/* Get shared message buffer */
		ret = threadx_i2c_get_msg_buffer(i2c_dev);
		if (ret) {
			dev_err(&adap->dev,
				"failed to get shared message buffer\n");
			return ret;
		}
		dev_dbg(&adap->dev, "got shared message buffer: %lu at %p\n",
			i2c_dev->msg_buffer_size, i2c_dev->msg_buffer);
	}

	/* We do not support more than two messages for transfer */
	if (num > 2) {
		dev_err(&adap->dev, "do not support more than 2 messages!\n");
		return -EINVAL;
	}

	/* For transfer with two messages, we only support Write, followed by a
	 * read.
	 */
	if (num == 2 &&
	    ((msgs[0].flags & I2C_M_RD) || (!(msgs[1].flags & I2C_M_RD)))) {
		dev_err(&adap->dev, "only support write followed by read\n");
		return -EINVAL;
	}

	/* Prepare transfer buffers */
	if (msgs->flags & I2C_M_RD) {
		tx_size = 0;
		rx_size = msgs->len;
		rx_buf = msgs->buf;
	} else {
		tx_size = msgs->len;
		rx_size = num == 2 ? msgs[1].len : 0;
		rx_buf = num == 2 ? msgs[1].buf : NULL;
	}

	/* Cannot transfer more than message shared buffer size */
	if (psi2c_check_size(i2c_dev->msg_buffer_size, tx_size, rx_size)) {
		dev_err(&adap->dev, "cannot handle message bigger than %lu\n",
			i2c_dev->msg_buffer_size);
		return -ENOMEM;
	}

	/* Wait for PSI2C access */
	mutex_lock(&i2c_dev->lock);

	/* Copy write message data to shared message buffer */
	psi2c_set_tx(i2c_dev->msg_buffer, msgs->buf, tx_size, rx_size);

	/* Prepare message */
	msg.cmd = PSI2C_CMD_TRANSFER;
	msg.channel = i2c_dev->adapter.nr;
	msg.xfer.slave_address = msgs->addr;
	msg.xfer.tx_size = tx_size;
	msg.xfer.rx_size = rx_size;

	/* Send request for shared message buffer */
	i2c_dev->state = PSI2C_STATE_NONE;
	rpmsg_send(rp_device->ept, &msg, sizeof(msg));

	/* Wait for end of transfer */
	ret = wait_event_interruptible_timeout(
					     i2c_dev->msg_queue,
					     i2c_dev->state != PSI2C_STATE_NONE,
					     THREADX_I2C_TIMEOUT);
	if (ret <= 0 || i2c_dev->state != PSI2C_STATE_SUCCESS) {
		mutex_unlock(&i2c_dev->lock);
		return ret < 0 ? ret : -EIO;
	}

	/* Copy received data */
	psi2c_get_rx(i2c_dev->msg_buffer, rx_buf, tx_size, rx_size);

	/* Unlock PSI2C access */
	mutex_unlock(&i2c_dev->lock);

	return num;
}

static u32 threadx_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm threadx_i2c_algo = {
	.master_xfer	= threadx_i2c_xfer,
	.functionality	= threadx_i2c_func,
};

static int threadx_i2c_probe(struct platform_device *pdev)
{
	struct threadx_i2c_dev *i2c_dev;
	int ret;

	/* Check I2C channel */
	if (pdev->id >= PSI2C_CHANNEL_COUNT)
		return -ENODEV;

	/* Allocate device structure */
	i2c_dev = devm_kzalloc(&pdev->dev, sizeof(*i2c_dev), GFP_KERNEL);
	if (!i2c_dev) {
		dev_err(&pdev->dev, "failed to allocate device\n");
		return -ENOMEM;
	}

	/* Fill private structure */
	i2c_dev->adapter.algo = &threadx_i2c_algo;
	i2c_dev->dev = &pdev->dev;

	platform_set_drvdata(pdev, i2c_dev);

	/* Set adapter configuration */
	i2c_set_adapdata(&i2c_dev->adapter, i2c_dev);
	i2c_dev->adapter.owner = THIS_MODULE;
	i2c_dev->adapter.class = I2C_CLASS_DEPRECATED;
	strlcpy(i2c_dev->adapter.name, dev_name(&pdev->dev),
		sizeof(i2c_dev->adapter.name));
	i2c_dev->adapter.dev.parent = &pdev->dev;
	i2c_dev->adapter.nr = pdev->id;
	i2c_dev->adapter.dev.of_node = pdev->dev.of_node;

	/* Initialize message wait queue */
	init_waitqueue_head(&i2c_dev->msg_queue);

	/* Initialize mutex for exclusive access to I2C bus */
	mutex_init(&i2c_dev->lock);

	/* Register I2C adapter */
	ret = i2c_add_numbered_adapter(&i2c_dev->adapter);
	if (ret) {
		dev_err(&pdev->dev, "failed adding I2C adapter!\n");
		return ret;
	}

	/* Add device to main list */
	i2c_devs[i2c_dev->adapter.nr] = i2c_dev;

	dev_info(&pdev->dev, "ThreadX I2C bus %d is ready",
		 i2c_dev->adapter.nr);

	return 0;
}

static int threadx_i2c_remove(struct platform_device *pdev)
{
	struct threadx_i2c_dev *i2c_dev = platform_get_drvdata(pdev);

	/* Unmap shared message buffer */
	threadx_i2c_unmap_msg_buffer(i2c_dev);

	i2c_devs[pdev->id] = NULL;
	i2c_del_adapter(&i2c_dev->adapter);

	return 0;
}

static const struct of_device_id threadx_i2c_of_match[] = {
	{.compatible = "threadx,i2c", },
	{},
};
MODULE_DEVICE_TABLE(of, threadx_i2c_of_match);

static struct platform_driver threadx_i2c_driver = {
	.probe		= threadx_i2c_probe,
	.remove		= threadx_i2c_remove,
	.driver		= {
		.name	= "threadx-i2c",
		.of_match_table = threadx_i2c_of_match,
	},
};

static int __init threadx_i2c_init(void)
{
	/* Register ThreadX I2C rpmsg channel for all I2C adpaters */
	register_rpmsg_driver(&threadx_i2c_rpmsg_driver);

	/* Register I2C driver */
	return platform_driver_register(&threadx_i2c_driver);
}

static void __exit threadx_i2c_exit(void)
{
	/* Unregister rpmsg channel */
	unregister_rpmsg_driver(&threadx_i2c_rpmsg_driver);

	/* Unregister I2C driver */
	platform_driver_unregister(&threadx_i2c_driver);
}

subsys_initcall(threadx_i2c_init);
module_exit(threadx_i2c_exit);

MODULE_DESCRIPTION("ThreadX shared I2C Bus Controller");
MODULE_AUTHOR("Alexandre Dilly <alexandre.dilly@parrot.com>");
MODULE_LICENSE("GPL v2");
