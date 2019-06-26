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
#include <linux/clk.h>
#include <linux/remoteproc.h>
#include <linux/clockchips.h>
#include <linux/cpufreq.h>

#include <plat/ambalink_cfg.h>
#include <plat/timer.h>
#include <plat/clk.h>
#include <plat/event.h>

#ifndef UINT32
typedef u32 UINT32;
#endif
#ifndef UINT64
typedef u64 UINT64;
#endif

typedef struct clk_name_s {
	int clk_idx;
	char clk_name[64];
} clk_name;

typedef enum _AMBA_RPDEV_CLK_CMD_e_ {
        CLK_SET = 0,
        CLK_GET,
        CLK_RPMSG_ACK_THREADX,
        CLK_RPMSG_ACK_LINUX,
        CLK_CHANGED_PRE_NOTIFY,
        CLK_CHANGED_POST_NOTIFY,
} AMBA_RPDEV_CLK_CMD_e;

typedef struct _AMBA_RPDEV_CLK_MSG_s_ {
	UINT32  Cmd;
	UINT64  Param;
} AMBA_RPDEV_CLK_MSG_s;

struct rpdev_clk_info {
	u32 clk_idx;
	u32 rate;
	u32 padding[6];
} __attribute__((aligned(32), packed));

DECLARE_COMPLETION(clk_comp);
struct rpmsg_device *rpdev_clk;

extern int hibernation_start;
u32 oldfreq, newfreq;

/* -------------------------------------------------------------------------- */
extern unsigned long loops_per_jiffy;
static inline unsigned int ambarella_adjust_jiffies(unsigned long val,
                                                    unsigned int oldfreq, unsigned int newfreq)
{
	if (((val == AMBA_EVENT_PRE_CPUFREQ) && (oldfreq < newfreq)) ||
	    ((val == AMBA_EVENT_POST_CPUFREQ) && (oldfreq != newfreq))) {
		loops_per_jiffy = cpufreq_scale(loops_per_jiffy,
						oldfreq, newfreq);

		return newfreq;
	}

	return oldfreq;
}

/* -------------------------------------------------------------------------- */
/* Received the ack from ThreadX. */
static int rpmsg_clk_ack_linux(void *data)
{
	complete(&clk_comp);

	return 0;
}

/* Ack Threadx, Linux is done. */
static int rpmsg_clk_ack_threadx(void *data)
{
	AMBA_RPDEV_CLK_MSG_s clk_ctrl_cmd = {0};

	clk_ctrl_cmd.Cmd = CLK_RPMSG_ACK_THREADX;

	return rpmsg_send(rpdev_clk->ept, &clk_ctrl_cmd, sizeof(clk_ctrl_cmd));
}

/* Pre-notify Linux, some clock will be changed. */
static int rpmsg_clk_changed_pre_notify(void *data)
{
	int retval = 0;

	retval = notifier_to_errno(
	                 ambarella_set_event(AMBA_EVENT_PRE_CPUFREQ, NULL));
	if (retval) {
		pr_err("%s: AMBA_EVENT_PRE_CPUFREQ failed(%d)\n",
			__func__, retval);
	}

	/* No need to handle clock event and clock source. */
	/* Since Cortex clock (CNTFRQ) will never be changed. */

	rpmsg_clk_ack_threadx(data);

	return 0;
}

/* Post-notify Linux, some clock has been changed. */
static int rpmsg_clk_changed_post_notify(void *data)
{
	int retval = 0;

	/* No need to handle clock event and clock source. */
	/* Since Cortex clock (CNTFRQ) will never be changed. */

	retval = notifier_to_errno(
	                 ambarella_set_event(AMBA_EVENT_POST_CPUFREQ, NULL));
	if (retval) {
		pr_err("%s: AMBA_EVENT_POST_CPUFREQ failed(%d)\n",
		       __func__, retval);
	}

	rpmsg_clk_ack_threadx(data);

	return 0;
}

/* -------------------------------------------------------------------------- */
typedef int (*PROC_FUNC)(void *data);
static PROC_FUNC proc_list[] = {
	rpmsg_clk_ack_linux,
	rpmsg_clk_changed_pre_notify,
	rpmsg_clk_changed_post_notify,
};

static int rpmsg_clk_cb(struct rpmsg_device *rpdev, void *data, int len,
                         void *priv, u32 src)
{
	int rval = 0;
	AMBA_RPDEV_CLK_MSG_s *msg = (AMBA_RPDEV_CLK_MSG_s *) data;

	switch (msg->Cmd) {
	case CLK_RPMSG_ACK_LINUX:
		rval = proc_list[0](data);
		break;
	case CLK_CHANGED_PRE_NOTIFY:
		rval = proc_list[1](data);
		break;
	case CLK_CHANGED_POST_NOTIFY:
		rval = proc_list[2](data);
		break;
	default:
		break;
	}

	return rval;
}

static int rpmsg_clk_probe(struct rpmsg_device *rpdev)
{
	int ret = 0;
	struct rpmsg_channel_info chinfo;

	//printk("%s: probed", __func__);

	rpdev_clk = rpdev;

	strncpy(chinfo.name, rpdev->id.name, sizeof(chinfo.name));
	chinfo.src = RPMSG_ADDR_ANY;
	chinfo.dst = rpdev->dst;

	rpmsg_send(rpdev->ept, &chinfo, sizeof(chinfo));

	return ret;
}

static void rpmsg_clk_remove(struct rpmsg_device *rpdev)
{
}

static struct rpmsg_device_id rpmsg_clk_id_table[] = {
	{ .name	= "AmbaRpdev_CLK", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_clk_id_table);

static struct rpmsg_driver rpmsg_clk_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_clk_id_table,
	.probe		= rpmsg_clk_probe,
	.callback	= rpmsg_clk_cb,
	.remove		= rpmsg_clk_remove,
};

static int __init rpmsg_clk_init(void)
{
	return register_rpmsg_driver(&rpmsg_clk_driver);
}

static void __exit rpmsg_clk_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_clk_driver);
}

fs_initcall(rpmsg_clk_init);
module_exit(rpmsg_clk_fini);

MODULE_DESCRIPTION("RPMSG CLK Server");
