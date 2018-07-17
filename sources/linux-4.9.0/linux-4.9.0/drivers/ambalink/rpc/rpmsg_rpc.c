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
#include <linux/aipc_msg.h>

#ifdef RPC_DEBUG
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <plat/ambalink_cfg.h>
#endif
#include "aipc_priv.h"

#define chnl_tx_name "aipc_rpc"

static struct rpmsg_device *chnl_tx;

#ifdef RPC_DEBUG
#define RPC_MACIG 0xD0
#define READ_IOCTL _IOR(RPC_MACIG, 0, int)
#define WRITE_IOCTL _IOW(RPC_MACIG, 1, int)
static int rpc_major;
static int rpc_debug = 0;
static const char proc_name[] = "rpc_debug";
static struct proc_dir_entry *rpc_file = NULL;
char profile_buf[200];

extern unsigned int read_aipc_timer(void);
/*
 * calculate the time
 */
static unsigned int calc_timer_diff(unsigned int start, unsigned int end)
{
	unsigned int diff;
	if (end <= start) {
		diff = start - end;
	} else {
		diff = 0xFFFFFFFF - end + 1 + start;
	}
	return diff;
}

static void read_timer(unsigned long arg)
{
	unsigned long ret;
	memset(profile_buf, 0x0, sizeof(profile_buf));
	sprintf(profile_buf, "%u", read_aipc_timer());
	ret = copy_to_user((char *)arg, profile_buf, strlen(profile_buf)+1);
}

static void wrtie_profile(void)
{
	unsigned int addr, result, cur_time, diff;
	unsigned int* value, *sec_value;
	int cond;

	/* access to the statistics in shared memory*/
	sscanf(profile_buf, "%d %u %u", &cond, &addr, &result);
	value = (unsigned int *) phys_to_virt(addr + ambalink_shm_layout.rpc_profile_addr);
	switch (cond) {
	case 1: //add the result
		*value += result;
		break;
	case 2: //identify whether the result is larger than value
		if (*value < result) {
			*value = result;
		}
		break;
	case 3: //identify whether the result is smaller than value
		if (*value > result) {
			*value = result;
		}
		break;
	case 4: //calculate injection time
		//value is LuLastInjectTime & sec_value is LuTotalInjectTime
		sec_value = (unsigned int *) phys_to_virt(result + ambalink_shm_layout.rpc_profile_addr);
		cur_time = read_aipc_timer();
		if ( *value != 0) {
			//calculate the duration from last to current injection.
			diff = calc_timer_diff(*value, cur_time);
		} else {
			diff = 0;
		}
		*sec_value += diff; //sum up the injection time
		*value = cur_time;
		break;
	}
}
/*
 * access the rpc statistics through ioctl
 */
static long rpmsg_rpc_profile_ioctl(struct file *filep, unsigned int cmd,
	unsigned long arg)
{
	long len = 200;
	unsigned long ret;

	switch(cmd) {
		case READ_IOCTL:
			read_timer(arg);
			break;
		case WRITE_IOCTL:
			ret = copy_from_user(profile_buf, (char *)arg, len);
			wrtie_profile();
			break;
		default:
			return -ENOTTY;
	}
	return len;
}

static struct file_operations fops = {
	.unlocked_ioctl = rpmsg_rpc_profile_ioctl,
	.compat_ioctl = rpmsg_rpc_profile_ioctl,
};

static ssize_t rpmsg_rpc_proc_write(struct file *file,
                                const char __user *buffer, size_t count, loff_t *data)
{
	char usr_input[10];
	int retval;

	if (count > sizeof(usr_input)) {
		pr_err("%s: count %d out of size!\n", __func__, (u32)count);
		retval = -ENOSPC;
		goto rpmsg_rpc_write_exit;
	}


	if (copy_from_user(usr_input, buffer, count)) {
		pr_err("%s: copy_from_user fail!\n", __func__);
		retval = -EFAULT;
		goto rpmsg_rpc_write_exit;
	}

	sscanf(usr_input,"%d", &rpc_debug);
	retval = count;

rpmsg_rpc_write_exit:
	return retval;
}

static int rpmsg_rpc_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%d", rpc_debug);
	return 0;
}

static int rpmsg_rpc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, rpmsg_rpc_proc_show, PDE_DATA(inode));
}

static const struct file_operations proc_rpmsg_rpc_fops = {
	.open = rpmsg_rpc_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = rpmsg_rpc_proc_write,
	.release = single_release,
};
#endif
/*
 * forward incoming packet from remote to router
 */
static int rpmsg_rpc_recv(struct rpmsg_device *rpdev, void *data, int len,
                           void *priv, u32 src)
{
#ifdef RPC_DEBUG
	struct aipc_pkt *pkt = (struct aipc_pkt *)data;
	pkt->xprt.lk_to_lu_start = read_aipc_timer();
#endif
	DMSG("rpmsg_rpc recv %d bytes\n", len);
	aipc_router_send((struct aipc_pkt*)data, len);

	return 0;
}

/*
 * send out packet targeting ThreadX
 */
static void rpmsg_rpc_send_tx(struct aipc_pkt *pkt, int len, int port)
{
	if (chnl_tx) {
#ifdef RPC_DEBUG
		pkt->xprt.lu_to_lk_end = read_aipc_timer();
#endif
		rpmsg_send(chnl_tx->ept, pkt, len);
	}
}

static int rpmsg_rpc_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;
#ifdef RPC_DEBUG
	rpc_file = proc_create_data(proc_name, S_IRUGO | S_IWUSR,
	                            get_ambarella_proc_dir(),
	                            &proc_rpmsg_rpc_fops, NULL);
	if (rpc_file == NULL) {
		pr_err("%s: %s fail!\n", __func__, proc_name);
		ret = -ENOMEM;
	}
#endif
	if (!strcmp(rpdev->id.name, chnl_tx_name))
		chnl_tx = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_rpc_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_rpc_id_table[] = {
	{ .name = chnl_tx_name, },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_rpc_id_table);

static struct rpmsg_driver rpmsg_rpc_driver = {
	.drv.name   = KBUILD_MODNAME,
	.drv.owner  = THIS_MODULE,
	.id_table   = rpmsg_rpc_id_table,
	.probe      = rpmsg_rpc_probe,
	.callback   = rpmsg_rpc_recv,
	.remove     = rpmsg_rpc_remove,
};

static int __init rpmsg_rpc_init(void)
{
	struct xprt_ops ops_tx = {
		.send = rpmsg_rpc_send_tx,
	};
	aipc_router_register_xprt(AIPC_HOST_THREADX, &ops_tx);

#ifdef RPC_DEBUG
	rpc_major = register_chrdev(0, "rpc_profile", &fops);
	if (rpc_major < 0) {
		printk ("Registering the rpc profile device failed with %d\n", rpc_major);
	} else {
		printk (KERN_INFO "Registering the rpc profile device successfully with %d\n", rpc_major);
	}
#endif
	return register_rpmsg_driver(&rpmsg_rpc_driver);
}

static void __exit rpmsg_rpc_fini(void)
{
#ifdef RPC_DEBUG
	unregister_chrdev(rpc_major, "rpc_profile");
#endif
	unregister_rpmsg_driver(&rpmsg_rpc_driver);
}

module_init(rpmsg_rpc_init);
module_exit(rpmsg_rpc_fini);

MODULE_DESCRIPTION("RPMSG RPC Server");
