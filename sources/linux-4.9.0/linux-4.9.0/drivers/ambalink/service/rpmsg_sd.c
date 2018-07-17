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

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/remoteproc.h>

#include <linux/aipc/rpmsg_sd.h>
#include <plat/sd.h>
#include <plat/ambalink_cfg.h>


typedef struct _AMBA_RPDEV_SD_MSG_s_ {
	u64	Cmd;
	u64	Param;
} AMBA_RPDEV_SD_MSG_s;

DECLARE_COMPLETION(sd0_comp);
DECLARE_COMPLETION(sd1_comp);
struct rpmsg_device *rpdev_sd;

//#define AMBARELLA_RPMSG_SD_PROC
#ifdef AMBARELLA_RPMSG_SD_PROC

static const char sd_proc_name[] = "sd";
static struct proc_dir_entry *rpmsg_proc_dir = NULL;
static struct proc_dir_entry *sd_file = NULL;

static int rpmsg_sd_proc_read(char *page, char **start,
                              off_t off, int count, int *eof, void *data)
{
	return 0;
}

static int rpmsg_sd_proc_write(struct file *file,
                               const char __user *buffer, unsigned long count, void *data)
{
	int retval, i;
	char sd_buf[256];
	struct rpmsg_device *rpdev = data;

	if (count > sizeof(sd_buf)) {
		pr_err("%s: count %d out of size!\n", __func__, (u32)count);
		retval = -ENOSPC;
		goto rpmsg_sd_write_exit;
	}

	memset(sd_buf, 0x0, sizeof(sd_buf));

	if (copy_from_user(sd_buf, buffer, count)) {
		pr_err("%s: copy_from_user fail!\n", __func__);
		retval = -EFAULT;
		goto rpmsg_sd_write_exit;
	}

	/* Strip trailing \r or \n */
	for (i = count - 1 ; i >= 0; i--) {
		if (sd_buf[i] == '\r' || sd_buf[i] == '\n') {
			sd_buf[i] = '\0';
			count--;
		} else {
			break;
		}
	}

	count ++;

	//printk("[%s]: sd_buf: %s, count = %d\n", __func__, sd_buf, (int) count);

	rpmsg_send(rpdev->ept, sd_buf, count);

	retval = count;

rpmsg_sd_write_exit:
	return count;
}
#endif

/* -------------------------------------------------------------------------- */
int rpmsg_sdinfo_get(void *data)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;
	struct rpdev_sdinfo *sdinfo;

	BUG_ON(!rpdev_sd);

	sdinfo = (struct rpdev_sdinfo *) data;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_INFO_GET;
	sd_ctrl_cmd.Param = (u64) ambalink_virt_to_phys((unsigned long) data);

	rpmsg_send(rpdev_sd->ept, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	if (sdinfo->host_id == SD_HOST_0) {
		wait_for_completion(&sd0_comp);
	} else {
		wait_for_completion(&sd1_comp);
	}

	return 0;
}
EXPORT_SYMBOL(rpmsg_sdinfo_get);

int rpmsg_sdresp_get(void *data)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;
	struct rpdev_sdresp *sdresp;

	BUG_ON(!rpdev_sd);

	sdresp = (struct rpdev_sdresp *) data;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_RESP_GET;
	sd_ctrl_cmd.Param = (u64) ambalink_virt_to_phys((unsigned long) data);

	rpmsg_send(rpdev_sd->ept, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	if (sdresp->host_id == SD_HOST_0) {
		wait_for_completion(&sd0_comp);
	} else {
		wait_for_completion(&sd1_comp);
	}

	return 0;
}
EXPORT_SYMBOL(rpmsg_sdresp_get);

int rpmsg_sd_detect_insert(u32 host_id)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_DETECT_INSERT;
	sd_ctrl_cmd.Param = host_id;

	rpmsg_send(rpdev_sd->ept, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sd_detect_insert);

int rpmsg_sd_detect_eject(u32 host_id)
{
	AMBA_RPDEV_SD_MSG_s sd_ctrl_cmd;

	memset(&sd_ctrl_cmd, 0x0, sizeof(sd_ctrl_cmd));
	sd_ctrl_cmd.Cmd = SD_DETECT_EJECT;
	sd_ctrl_cmd.Param = host_id;

	rpmsg_send(rpdev_sd->ept, &sd_ctrl_cmd, sizeof(sd_ctrl_cmd));

	return 0;
}
EXPORT_SYMBOL(rpmsg_sd_detect_eject);

/* -------------------------------------------------------------------------- */

static int rpmsg_sdresp_detect_change(void *data)
{
#ifdef CONFIG_MMC_AMBARELLA
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;
	extern void ambarella_sd_rpmsg_cd(int host_id);

	ambarella_sd_rpmsg_cd((int) msg->Param);
#endif

	return 0;
}

static int rpmsg_sd_ack(void *data)
{
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;

	if (msg->Param == SD_HOST_0) {
		complete(&sd0_comp);
	} else {
		complete(&sd1_comp);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
typedef int (*PROC_FUNC)(void *data);
static PROC_FUNC proc_list[] = {
	rpmsg_sdresp_detect_change,
	rpmsg_sd_ack,
};

static int rpmsg_sd_cb(struct rpmsg_device *rpdev, void *data, int len,
                        void *priv, u32 src)
{
	int rval = 0;
	AMBA_RPDEV_SD_MSG_s *msg = (AMBA_RPDEV_SD_MSG_s *) data;

#if 0
	printk("recv: cmd = [%d], data = [0x%08x]", msg->Cmd, msg->Param);
#endif
	switch (msg->Cmd) {
	case SD_DETECT_CHANGE:
		rval = proc_list[0](data);
		break;
	case SD_RPMSG_ACK:
		rval = proc_list[1](data);
		break;
	default:
		rval = -1;
		printk("%s err: cmd = [0x%08llx], data = [0x%08llx]",
		       __func__, msg->Cmd, msg->Param);
		break;
	}

	return rval;
}

#ifdef AMBARELLA_RPMSG_SD_PROC
static const struct file_operations proc_rpmsg_sd_fops = {
	.read = seq_read,
	.write = rpmsg_sd_proc_write,
};
#endif

static int rpmsg_sd_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;

	//printk("%s: probed\n", __func__);

#ifdef AMBARELLA_RPMSG_SD_PROC
	rpmsg_proc_dir = proc_mkdir("rpmsg_sd", get_ambarella_proc_dir());

	sd_file = proc_create_data(sd_proc_name, S_IRUGO | S_IWUSR,
	                           rpmsg_proc_dir,
	                           &proc_rpmsg_sd_fops, NULL);
	if (sd_file == NULL) {
		pr_err("%s: %s fail!\n", __func__, sd_proc_name);
		ret = -ENOMEM;
	} else {
		sd_file->data = (void *) rpdev;
	}
#endif

	rpdev_sd = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_sd_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_sd_id_table[] = {
	{ .name = "AmbaRpdev_SD", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_sd_id_table);

static struct rpmsg_driver rpmsg_sd_driver = {
	.drv.name   = KBUILD_MODNAME,
	.drv.owner  = THIS_MODULE,
	.id_table   = rpmsg_sd_id_table,
	.probe      = rpmsg_sd_probe,
	.callback   = rpmsg_sd_cb,
	.remove     = rpmsg_sd_remove,
};

static int __init rpmsg_sd_init(void)
{
	return register_rpmsg_driver(&rpmsg_sd_driver);
}

static void __exit rpmsg_sd_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_sd_driver);
}

fs_initcall(rpmsg_sd_init);
module_exit(rpmsg_sd_fini);

MODULE_DESCRIPTION("RPMSG SD Server");
