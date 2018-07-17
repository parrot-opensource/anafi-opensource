/*
 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
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
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>

static struct rpmsg_device *rpdev_example;
static char *example_printk = "written Message will be printed on Remote Processor";

static int rpmsg_example_printk(const char *val, const struct kernel_param *kp)
{
	char *data = strstrip((char *)val);

	rpmsg_send(rpdev_example->ept, data, strlen(data) + 1);

	return param_set_charp(data, kp);
}

static struct kernel_param_ops param_ops_example_printk = {
	/*.set = param_set_charp,*/
	.set = rpmsg_example_printk,
	.get = param_get_charp,
	.free = param_free_charp,
};

module_param_cb(example_printk, &param_ops_example_printk,
	&(example_printk), 0644);

static int rpmsg_echo_cb(struct rpmsg_device *rpdev, void *data, int len,
			void *priv, u32 src)
{
	printk("[ %20s ] recv msg: [%s] from 0x%x and len %d\n",
	       __func__, (const char*)data, src, len);

	/* Echo the recved message back */
	return rpmsg_send(rpdev->ept, data, len);
}

static int rpmsg_echo_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;
	rpdev_example = rpdev;
	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_echo_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_echo_id_table[] = {
	{ .name	= "echo_ca9_b", },
	{ .name	= "echo_arm11", },
	{ .name	= "echo_cortex", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_echo_id_table);

static struct rpmsg_driver rpmsg_echo_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_echo_id_table,
	.probe		= rpmsg_echo_probe,
	.callback	= rpmsg_echo_cb,
	.remove		= rpmsg_echo_remove,
};

static int __init rpmsg_echo_init(void)
{
	return register_rpmsg_driver(&rpmsg_echo_driver);
}

static void __exit rpmsg_echo_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_echo_driver);
}

module_init(rpmsg_echo_init);
module_exit(rpmsg_echo_fini);

MODULE_DESCRIPTION("RPMSG Echo Server");
