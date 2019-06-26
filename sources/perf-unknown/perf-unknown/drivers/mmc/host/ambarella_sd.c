/*
 * drivers/mmc/host/ambarella_sd.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 * Copyright (C) 2004-2009, Ambarella, Inc.
 *
 * History:
 *	2016/03/03 - [Cao Rongrong] Re-write the driver
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
#include <linux/init.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/debugfs.h>
#include <plat/rct.h>
#include <plat/sd.h>
#include <plat/event.h>
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
#include <linux/aipc/ipc_mutex.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#endif
#if defined(CONFIG_AMBALINK_SD)
#include <linux/aipc/rpmsg_sd.h>

static struct rpdev_sdinfo G_rpdev_sdinfo[SD_INSTANCES];
#endif

#if defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
static struct mmc_host *G_mmc[SD_INSTANCES];
#endif

/* ==========================================================================*/
struct ambarella_sd_dma_desc {
	u16 attr;
	u16 len;
	u32 addr;
} __attribute((packed));

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
union amba_sd_detail_delay {
	u32 data;
	struct{
		u32 rd_latency:      2;
		u32 rx_clk_pol:      1;
		u32 clk_out_bypass:  1;
		u32 data_cmd_bypass: 1;
		u32 sel_value:       8;
		u32 sbc_core_delay:  4;
		u32 din_clk_pol:     4;
		u32 rev:            14;
	} bits;
};
#endif

struct ambarella_mmc_host {
	unsigned char __iomem 		*regbase;
	unsigned char __iomem 		*fio_reg;
	unsigned char __iomem 		*phy_ctrl0_reg;
	unsigned char __iomem 		*phy_ctrl1_reg;
	unsigned char __iomem 		*phy_ctrl2_reg;
	struct device			*dev;
	unsigned int			irq;

	struct mmc_host			*mmc;
	struct mmc_request		*mrq;
	struct mmc_command		*cmd;
	struct dentry			*debugfs;

	spinlock_t			lock;
	struct tasklet_struct		finish_tasklet;
	struct timer_list		timer;
	u32				irq_status;
	u32				ac12es;

	struct clk			*clk;
	u32				clock;
	u32				ns_in_one_cycle;
	u8				power_mode;
	u8				bus_width;
	u8				mode;

	struct ambarella_sd_dma_desc	*desc_virt;
	dma_addr_t			desc_phys;

	u32				switch_1v8_dly;
	int				fixed_cd;
	int				fixed_wp;
	bool				emmc_boot;
	bool				auto_cmd12;

	int				pwr_gpio;
	u8				pwr_gpio_active;
	int				v18_gpio;
	u8				v18_gpio_active;

	struct notifier_block		system_event;
	struct semaphore		system_event_sem;

#ifdef CONFIG_PM
	u32				sd_nisen;
	u32				sd_eisen;
	u32				sd_nixen;
	u32				sd_eixen;
#endif
#if defined(CONFIG_AMBALINK_SD)
	struct delayed_work		detect;
	u32				insert;
#endif
#if defined(CONFIG_MMC_AMBARELLA_DELAY)
	union amba_sd_detail_delay	detail_delay;
#endif

};

#if defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
/* --- /sys/module/ambarella_sd/parameters/ */
#define SDIO_GLOBAL_ID 2
static int force_sdio_host_high_speed = -1;
static int sdio_host_max_frequency = -1;
static int sdio_clk_ds = -1;
static int sdio_data_ds = -1;
static int sdio_cmd_ds = -1;
static int sdio_host_odly = -1;
static int sdio_host_ocly = -1;
static int sdio_host_idly = -1;

int ambarella_set_sdio_host_max_frequency(const char *str, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_high_speed(const char *val, const struct kernel_param *kp);
extern int amba_sdio_delay_post_apply(const int odly, const int ocly,
	const int idly);
extern int amba_sdio_ds_post_apply(const int clk_ds, const int data_ds,
	const int cmd_ds);
extern int ambarella_set_sdio_clk_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_data_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_cmd_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_odly(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_ocly(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_idly(const char *val, const struct kernel_param *kp);
extern const struct file_operations proc_fops_sdio_info;

static struct kernel_param_ops param_ops_sdio_host_high_speed = {
	.set = ambarella_set_sdio_host_high_speed,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_max_frequency = {
	.set = ambarella_set_sdio_host_max_frequency,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_clk_ds = {
	.set = ambarella_set_sdio_clk_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_data_ds = {
	.set = ambarella_set_sdio_data_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_cmd_ds = {
	.set = ambarella_set_sdio_cmd_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_odly = {
	.set = ambarella_set_sdio_host_odly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_ocly = {
	.set = ambarella_set_sdio_host_ocly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_idly = {
	.set = ambarella_set_sdio_host_idly,
	.get = param_get_int,
};

module_param_cb(force_sdio_host_high_speed, &param_ops_sdio_host_high_speed,
	&(force_sdio_host_high_speed), 0644);
module_param_cb(sdio_host_max_frequency, &param_ops_sdio_host_max_frequency,
	&(sdio_host_max_frequency), 0644);
module_param_cb(sdio_clk_ds, &param_ops_sdio_clk_ds,
	&(sdio_clk_ds), 0644);
module_param_cb(sdio_data_ds, &param_ops_sdio_data_ds,
	&(sdio_data_ds), 0644);
module_param_cb(sdio_cmd_ds, &param_ops_sdio_cmd_ds,
	&(sdio_cmd_ds), 0644);
module_param_cb(sdio_host_odly, &param_ops_sdio_host_odly,
	&(sdio_host_odly), 0644);
module_param_cb(sdio_host_ocly, &param_ops_sdio_host_ocly,
	&(sdio_host_ocly), 0644);
module_param_cb(sdio_host_idly, &param_ops_sdio_host_idly,
	&(sdio_host_idly), 0644);

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
struct amba_sd_phy_ctrl0_reg {
	u32 reserved_0:              17;     /* [16:0] Reserved */
	u32 sd_dll_bypass:           1;      /* [17] sd_dll_bypass */
	u32 sd_data_cmd_bypass:      1;      /* [18] sd_data_cmd_bypass */
	u32 rx_clk_polarity:         1;      /* [19] rx_clk_pol */
	u32 sd_duty_select:          2;      /* [21:20] sd_duty_sel[1:0] */
	u32 delay_chain_select:      2;      /* [23:22] delay_chain_sel[1:0] */
	u32 clk270_alone:            1;      /* [24] clk270_alone */
	u32 reset:                   1;      /* [25] rst */
	u32 sd_clk_out_bypass:       1;      /* [26] sd_clkout_bypass */
	u32 sd_din_clk_polarity:     1;      /* [27] auto_cmd_bypass_en */
	u32 reserved_1:              4;      /* [31:28] Reserved */
};

struct amba_sd_phy_ctrl1_reg {
	u32 sd_select0:              8;      /* [7:0] rct_sd_sel0[7:0] */
	u32 sd_select1:              8;      /* [15:8] rct_sd_sel1[7:0] */
	u32 sd_select2:              8;      /* [23:16] rct_sd_sel2[7:0] */
	u32 sd_select3:              8;      /* [31:24] rct_sd_sel3[7:0] */
};

struct amba_sd_phy_ctrl2_reg {
	u32  enable_dll:                 1;      /* [0] enable DLL*/
	u32  coarse_delay_step:          3;      /* [3:1] adjust coarse delay with 1ns step */
	u32  lock_range:                 2;      /* [5:4] adjust lock range */
	u32  internal_clk:               1;      /* [6] select 1/16 internal clock */
	u32  pd_bb:                      1;      /* [7] select PD-bb */
	u32  reserved_0:                 3;      /* [10:8] Reserved */
	u32  bypass_filter:              1;      /* [11] bypass filter when asserted high */
	u32  select_fsm:                 1;      /* [12] Select FSM */
	u32  force_lock_vshift:          1;      /* [13] force lock when vshift=1F */
	u32  force_lock_cycle:           1;      /* [14] force lock after 32 internal cycles */
	u32  power_down_shift:           1;      /* [15] power down shift register when locked */
	u32  enable_auto_coarse:         1;      /* [16] Enable automatic coarse adjustment */
	u32  reserved_1:                15;     /* [31:17] Reserved */
};

struct amba_sd_lat_ctrl_reg {
	u32 delay_cycle:            16;
	u32 reserved:               16;
};

static int ambarella_sd_detail_delay(struct ambarella_mmc_host *host)
{
	volatile struct amba_sd_phy_ctrl0_reg *phy_ctrl0_reg;
	volatile struct amba_sd_phy_ctrl1_reg *phy_ctrl1_reg;
	volatile struct amba_sd_phy_ctrl2_reg *phy_ctrl2_reg;
	volatile struct amba_sd_lat_ctrl_reg *lat_ctrl_reg;
	union amba_sd_detail_delay detail_delay;

	if (!host->phy_ctrl0_reg ||
		!host->phy_ctrl1_reg ||
		!host->phy_ctrl2_reg ||
		!host->regbase)
		return 0;

	phy_ctrl0_reg = (struct amba_sd_phy_ctrl0_reg *)host->phy_ctrl0_reg;
	phy_ctrl1_reg = (struct amba_sd_phy_ctrl1_reg *)host->phy_ctrl1_reg;
	phy_ctrl2_reg = (struct amba_sd_phy_ctrl2_reg *)host->phy_ctrl2_reg;
	lat_ctrl_reg = (struct amba_sd_lat_ctrl_reg *)(host->regbase + SD_LAT_CTRL_OFFSET);
	detail_delay = host->detail_delay;

	/* Phy Reset */
	writel_relaxed(0x0, phy_ctrl0_reg);
	phy_ctrl0_reg->sd_clk_out_bypass = 1;
	phy_ctrl0_reg->sd_data_cmd_bypass = 1;
	phy_ctrl0_reg->sd_dll_bypass = 1;

	/* Phy Sel Reset */
	writel_relaxed(0x0, phy_ctrl1_reg);

	/* Rd latency reset */
	lat_ctrl_reg->delay_cycle = 0;

	/* Apply delay */
	phy_ctrl0_reg->sd_din_clk_polarity =
		detail_delay.bits.din_clk_pol;
	phy_ctrl0_reg->rx_clk_polarity =
		detail_delay.bits.rx_clk_pol;
	phy_ctrl0_reg->sd_clk_out_bypass =
		detail_delay.bits.clk_out_bypass;
	phy_ctrl0_reg->sd_data_cmd_bypass =
		detail_delay.bits.data_cmd_bypass;
	phy_ctrl0_reg->sd_dll_bypass = 0;

	phy_ctrl2_reg->coarse_delay_step =
		detail_delay.bits.sbc_core_delay;
	phy_ctrl2_reg->enable_dll = 1;

	phy_ctrl1_reg->sd_select0 = detail_delay.bits.sel_value;
	phy_ctrl1_reg->sd_select1 = detail_delay.bits.sel_value;
	phy_ctrl1_reg->sd_select2 = detail_delay.bits.sel_value;

	phy_ctrl0_reg->reset = 1;
	udelay(15);
	phy_ctrl0_reg->reset = 0;
	udelay(15);

	/* Rd latency reset */
	lat_ctrl_reg->delay_cycle =
		(detail_delay.bits.rd_latency << 12) |
		(detail_delay.bits.rd_latency << 8) |
		(detail_delay.bits.rd_latency << 4) |
		(detail_delay.bits.rd_latency);

	return 0;
}
#endif


int ambarella_set_sdio_host_max_frequency(const char *str, const struct kernel_param *kp)
{
	int retval;
	unsigned int value;
	struct mmc_host *mmc = G_mmc[SDIO_GLOBAL_ID];

	param_set_uint(str, kp);

	retval = kstrtou32(str, 10, &value);

	mmc->f_max = value;
	pr_debug("mmc->f_max = %u\n", value);

	return retval;
}
/* +++ /sys/module/ambarella_sd/parameters/ */

/* --- /proc/ambarella/mmc_fixed_cd */
static int mmc_fixed_cd_proc_read(struct seq_file *m, void *v)
{
	int mmci;
	struct mmc_host *mmc;
	struct ambarella_mmc_host *host;

	for(mmci = 0; mmci < SD_INSTANCES; mmci++) {
		if (G_mmc[mmci]) {
			mmc = G_mmc[mmci];
			host = mmc_priv(mmc);
			seq_printf(m, "mmc%d fixed_cd=%d\n", mmci,
					host->fixed_cd);
		}
	}

	return 0;
}

static int mmc_fixed_cd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_fixed_cd_proc_read, NULL);
}

static ssize_t mmc_fixed_cd_proc_write(struct file *file,
                                const char __user *buffer, size_t count, loff_t *data)
{
	char input[4];
	int mmci;
	struct mmc_host *mmc;
	struct ambarella_mmc_host *host;

	if (count != 4) {
		printk(KERN_ERR "0 0=remove mmc0; 0 1=insert mmc0; 0 2=mmc0 auto\n");
		return -EFAULT;
	}

	if (copy_from_user(input, buffer, 3)) {
		printk(KERN_ERR "%s: copy_from_user fail!\n", __func__);
		return -EFAULT;
	}

	mmci = input[0] - '0';
	mmc = G_mmc[mmci];
	if (!mmc) {
		printk(KERN_ERR "%s: err!\n", __func__);
		return -EFAULT;
	}

	host = mmc_priv(mmc);

	host->mmc->caps &= ~MMC_CAP_NONREMOVABLE;
	host->fixed_cd = input[2] - '0';
	mmc_detect_change(mmc, 10);

	return count;
}

static const struct file_operations proc_fops_mmc_fixed_cd = {
	.open = mmc_fixed_cd_proc_open,
	.read = seq_read,
	.write = mmc_fixed_cd_proc_write,
	.llseek	= seq_lseek,
	.release = single_release,
};

/* init proc/sys entries */
static int ambarella_sd_info_init(struct mmc_host *mmc)
{
	u32 host_id;
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		(!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		SD_HOST_2;

	G_mmc[host_id] = mmc;

	if (host_id == SDIO_GLOBAL_ID) {
		int init_sd_para(size_t regbase);

		init_sd_para((size_t)host->regbase);
		proc_create("sdio_info", S_IRUGO | S_IWUSR, get_ambarella_proc_dir(),
			&proc_fops_sdio_info);
		proc_create("mmc_fixed_cd", S_IRUGO | S_IWUSR, get_ambarella_proc_dir(),
			&proc_fops_mmc_fixed_cd);
	}

	return 0;
}

/* deinit proc/sys entries */
static int ambarella_sd_info_deinit(struct mmc_host *mmc)
{
	u32 host_id;
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		(!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		SD_HOST_2;

	if (host_id == SDIO_GLOBAL_ID) {
		remove_proc_entry("mmc_fixed_cd", get_ambarella_proc_dir());
		remove_proc_entry("sdio_info", get_ambarella_proc_dir());
	}

	return 0;
}

/* +++ /proc/ambarella/mmc_fixed_cd */
#endif

#if defined(CONFIG_AMBALINK_SD)
void ambarella_sd_cd_detect(struct work_struct *work)
{
	struct ambarella_mmc_host *host =
		container_of(work, struct ambarella_mmc_host, detect.work);
	u32 host_id;

	host_id= (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		 (!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		 	SD_HOST_2;

	if (host->insert == 0x1) {
		rpmsg_sd_detect_insert(host_id);
	} else {
		rpmsg_sd_detect_eject(host_id);
	}
}

/**
 * Get the sdinfo inited by rpmsg.
 */
struct rpdev_sdinfo *ambarella_sd_sdinfo_get(struct mmc_host *mmc)
{
	u32 host_id;

	struct ambarella_mmc_host *host = mmc_priv(mmc);

	BUG_ON(!mmc);

	host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		  (!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		  	SD_HOST_2;

	return &G_rpdev_sdinfo[host_id];
}
EXPORT_SYMBOL(ambarella_sd_sdinfo_get);

/**
 * Send SD command through rpmsg.
 */
int ambarella_sd_rpmsg_cmd_send(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct rpdev_sdresp resp;
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	resp.opcode = cmd->opcode;
	resp.host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
			(!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
				SD_HOST_2;

	if (G_rpdev_sdinfo[resp.host_id].from_rpmsg == 0)
		return -1;

	/* send IPC */
	rpmsg_sdresp_get((void *) &resp);
	if (resp.ret != 0)
		return resp.ret;

	memcpy(cmd->resp, resp.resp, sizeof(u32) * 4);

	if (cmd->data != NULL) {
		memcpy(cmd->data->buf, resp.buf, cmd->data->blksz);
	}

	return 0;
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_cmd_send);

/**
 * Service initialization.
 */
int ambarella_sd_rpmsg_sdinfo_init(struct mmc_host *mmc)
{
	u32 host_id;
	struct rpdev_sdinfo sdinfo;
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		  (!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		  	SD_HOST_2;

	memset(&sdinfo, 0x0, sizeof(sdinfo));
	sdinfo.host_id = host_id;
	rpmsg_sdinfo_get((void *) &sdinfo);

	G_rpdev_sdinfo[host_id].is_init		= sdinfo.is_init;
	G_rpdev_sdinfo[host_id].is_sdmem	= sdinfo.is_sdmem;
	G_rpdev_sdinfo[host_id].is_mmc		= sdinfo.is_mmc;
	G_rpdev_sdinfo[host_id].is_sdio		= sdinfo.is_sdio;
	G_rpdev_sdinfo[host_id].bus_width	= sdinfo.bus_width;
	G_rpdev_sdinfo[host_id].clk		= sdinfo.clk;
	G_rpdev_sdinfo[host_id].ocr		= sdinfo.ocr;
	G_rpdev_sdinfo[host_id].hcs		= sdinfo.hcs;
	G_rpdev_sdinfo[host_id].rca		= sdinfo.rca;

	G_mmc[host_id] = mmc;

#if 0
	printk("%s: sdinfo.host_id   = %d\n", __func__, sdinfo.host_id);
	printk("%s: sdinfo.is_init   = %d\n", __func__, sdinfo.is_init);
	printk("%s: sdinfo.is_sdmem  = %d\n", __func__, sdinfo.is_sdmem);
	printk("%s: sdinfo.is_mmc    = %d\n", __func__, sdinfo.is_mmc);
	printk("%s: sdinfo.is_sdio   = %d\n", __func__, sdinfo.is_sdio);
	printk("%s: sdinfo.bus_width = %d\n", __func__, sdinfo.bus_width);
	printk("%s: sdinfo.clk       = %d\n", __func__, sdinfo.clk);
#endif
	return 0;
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_sdinfo_init);

/**
 * Enable to use the rpmsg sdinfo.
 */
void ambarella_sd_rpmsg_sdinfo_en(struct mmc_host *mmc, u8 enable)
{
	u32 host_id;
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	host_id = (!strcmp(host->dev->of_node->name, "sdmmc0")) ? SD_HOST_0 :
		  (!strcmp(host->dev->of_node->name, "sdmmc1")) ? SD_HOST_1 :
		  	SD_HOST_2;

	if (enable)
		G_rpdev_sdinfo[host_id].from_rpmsg = enable;
	else
		memset(&G_rpdev_sdinfo[host_id], 0x0, sizeof(struct rpdev_sdinfo));
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_sdinfo_en);

/**
 * ambarella_sd_rpmsg_cd
 */
void ambarella_sd_rpmsg_cd(int host_id)
{
	struct mmc_host *mmc = G_mmc[host_id];

	mmc_detect_change(mmc, msecs_to_jiffies(1000));
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_cd);

static void ambarella_sd_request_bus(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	down(&host->system_event_sem);

	/* Skip locking SD1 (SDIO for WiFi) and SD2. */
	/* Because they are not shared between dual OS. */
	if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
		aipc_mutex_lock(AMBA_IPC_MUTEX_SD0);

		disable_irq(host->irq);
		enable_irq(host->irq);
	} else if (!strcmp(host->dev->of_node->name, "sdmmc1")) {
		/*
		aipc_mutex_lock(AMBA_IPC_MUTEX_SD1);

		disable_irq(host->irq);
		enable_irq(host->irq);
		*/
	} else if (!strcmp(host->dev->of_node->name, "sdmmc2")) {
		/*
		aipc_mutex_lock(AMBA_IPC_MUTEX_SD2);

		disable_irq(host->irq);
		enable_irq(host->irq);
		*/
	} else {
		pr_err("%s: unknown SD host(%s)!!", __func__, host->dev->of_node->name);
	}
}

static void ambarella_sd_release_bus(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	/* Skip unlocking SD1 (SDIO for WiFi) and SD2. */
	/* Because they are not shared between dual OS. */
	if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
		aipc_mutex_unlock(AMBA_IPC_MUTEX_SD0);
	} else if (!strcmp(host->dev->of_node->name, "sdmmc1")) {
		//aipc_mutex_unlock(AMBA_IPC_MUTEX_SD1);
	} else if (!strcmp(host->dev->of_node->name, "sdmmc2")) {
		//aipc_mutex_unlock(AMBA_IPC_MUTEX_SD2);
	} else {
		pr_err("%s: unknown SD host(%s)!!", __func__, host->dev->of_node->name);
	}

	up(&host->system_event_sem);
}
#else
static void ambarella_sd_request_bus(struct mmc_host *mmc)
{
}

static void ambarella_sd_release_bus(struct mmc_host *mmc)
{
}
#endif

/* ==========================================================================*/
void ambarella_detect_sd_slot(int id, int fixed_cd)
{
	struct ambarella_mmc_host *host;
	struct platform_device *pdev;
	struct device_node *np;
	char tmp[4];

	if (!sd_slot_is_valid(id)) {
		pr_err("%s: Invalid slotid: %d\n", __func__, id);
		return;
	}

	snprintf(tmp, sizeof(tmp), "sd%d", id);

	np = of_find_node_by_path(tmp);
	if (!np) {
		pr_err("%s: No np found for %s\n", __func__, tmp);
		return;
	}

	pdev = of_find_device_by_node(np);
	if (!pdev) {
		pr_err("%s: No device found for %s\n", __func__, tmp);
		return;
	}

	host = platform_get_drvdata(pdev);
	host->fixed_cd = fixed_cd;
	mmc_detect_change(host->mmc, 0);
}
EXPORT_SYMBOL(ambarella_detect_sd_slot);

static ssize_t ambarella_fixed_cd_get(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	struct ambarella_mmc_host *host = file->private_data;
	char tmp[4];

	snprintf(tmp, sizeof(tmp), "%d\n", host->fixed_cd);
	tmp[3] = '\n';

	return simple_read_from_buffer(buf, count, ppos, tmp, sizeof(tmp));
}

static ssize_t ambarella_fixed_cd_set(struct file *file, const char __user *buf,
		size_t size, loff_t *ppos)
{
	struct ambarella_mmc_host *host = file->private_data;
	char tmp[20];
	ssize_t len;

	len = simple_write_to_buffer(tmp, sizeof(tmp) - 1, ppos, buf, size);
	if (len >= 0) {
		tmp[len] = '\0';
		host->fixed_cd = !!simple_strtoul(tmp, NULL, 0);
	}

	mmc_detect_change(host->mmc, 0);

	return len;
}

static const struct file_operations fixed_cd_fops = {
	.read	= ambarella_fixed_cd_get,
	.write	= ambarella_fixed_cd_set,
	.open	= simple_open,
	.llseek	= default_llseek,
};

static void ambarella_sd_add_debugfs(struct ambarella_mmc_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct dentry *root, *fixed_cd;

	if (!mmc->debugfs_root)
		return;

	root = debugfs_create_dir("ambhost", mmc->debugfs_root);
	if (IS_ERR_OR_NULL(root))
		return;

	host->debugfs = root;

	fixed_cd = debugfs_create_file("fixed_cd", S_IWUSR | S_IRUGO,
			host->debugfs, host, &fixed_cd_fops);
	if (IS_ERR_OR_NULL(fixed_cd)) {
		debugfs_remove_recursive(root);
		host->debugfs = NULL;
		dev_err(host->dev, "failed to add debugfs\n");
	}
}

static void ambarella_sd_remove_debugfs(struct ambarella_mmc_host *host)
{
	if (host->debugfs) {
		debugfs_remove_recursive(host->debugfs);
		host->debugfs = NULL;
	}
}

/* ==========================================================================*/

static void ambarella_sd_enable_irq(struct ambarella_mmc_host *host, u32 mask)
{
	u32 tmp;

	tmp = readl_relaxed(host->regbase + SD_NISEN_OFFSET);
	tmp |= mask;
	writel_relaxed(tmp, host->regbase + SD_NISEN_OFFSET);

	tmp = readl_relaxed(host->regbase + SD_NIXEN_OFFSET);
	tmp |= mask;
	writel_relaxed(tmp, host->regbase + SD_NIXEN_OFFSET);

}

static void ambarella_sd_disable_irq(struct ambarella_mmc_host *host, u32 mask)
{
	u32 tmp;

	tmp = readl_relaxed(host->regbase + SD_NISEN_OFFSET);
	tmp &= ~mask;
	writel_relaxed(tmp, host->regbase + SD_NISEN_OFFSET);

	tmp = readl_relaxed(host->regbase + SD_NIXEN_OFFSET);
	tmp &= ~mask;
	writel_relaxed(tmp, host->regbase + SD_NIXEN_OFFSET);
}

static void ambarella_sd_switch_clk(struct ambarella_mmc_host *host, bool enable)
{
	u32 clk_reg;

	clk_reg = readw_relaxed(host->regbase + SD_CLK_OFFSET);
	if (enable)
		clk_reg |= SD_CLK_EN;
	else
		clk_reg &= ~SD_CLK_EN;
	writew_relaxed(clk_reg, host->regbase + SD_CLK_OFFSET);

	/* delay to wait for SD_CLK output stable, this is somewhat useful. */
	udelay(10);
}

static void ambarella_sd_reset_all(struct ambarella_mmc_host *host)
{
	u32 nis, eis;
#if defined(CONFIG_AMBALINK_SD)
	u32 latency = 0, ctrl0_reg = 0, ctrl1_reg = 0, ctrl2_reg = 0;
#endif

	ambarella_sd_disable_irq(host, 0xffffffff);

#if defined(CONFIG_AMBALINK_SD)
	if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
		latency = readl_relaxed(host->regbase + SD_LAT_CTRL_OFFSET);
		if (host->phy_ctrl0_reg) {
			ctrl0_reg = readl_relaxed(host->phy_ctrl0_reg);
			ctrl1_reg = readl_relaxed(host->phy_ctrl1_reg);
			ctrl2_reg = readl_relaxed(host->phy_ctrl2_reg);
		}
	}
#endif

	/* reset sd phy timing if exist */
	if (host->phy_ctrl0_reg) {
		writel_relaxed(0x0, host->phy_ctrl2_reg);
		writel_relaxed(0x0, host->phy_ctrl1_reg);
		writel_relaxed(0x04070000, host->phy_ctrl0_reg);
	}

	writeb_relaxed(SD_RESET_ALL, host->regbase + SD_RESET_OFFSET);
	while (readb_relaxed(host->regbase + SD_RESET_OFFSET) & SD_RESET_ALL)
		cpu_relax();

#if defined(CONFIG_AMBALINK_SD)
	if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
		writel_relaxed(latency, host->regbase + SD_LAT_CTRL_OFFSET);
		if (host->phy_ctrl0_reg) {
			writel_relaxed(ctrl2_reg, host->phy_ctrl2_reg);
			writel_relaxed(ctrl1_reg, host->phy_ctrl1_reg);
			writel_relaxed(ctrl0_reg | SD_PHY_RESET, host->phy_ctrl0_reg);
			udelay(5); /* DLL reset time */
			writel_relaxed(ctrl0_reg, host->phy_ctrl0_reg);
			udelay(5); /* DLL lock time */
		}
	}
#endif

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
	/* If the detail delay is specified, use it to restore phy config */
	if (host->detail_delay.data) {
		ambarella_sd_detail_delay(host);
	}
#endif

	/* enable sd internal clock */
	writew_relaxed(SD_CLK_EN | SD_CLK_ICLK_EN, host->regbase + SD_CLK_OFFSET);
	while (!(readw_relaxed(host->regbase + SD_CLK_OFFSET) & SD_CLK_ICLK_STABLE))
		cpu_relax();

	nis = SD_NISEN_REMOVAL | SD_NISEN_INSERT |
		SD_NISEN_DMA | 	SD_NISEN_BLOCK_GAP |
		SD_NISEN_XFR_DONE | SD_NISEN_CMD_DONE;

	eis = SD_EISEN_ADMA_ERR | SD_EISEN_ACMD12_ERR |
		SD_EISEN_CURRENT_ERR | 	SD_EISEN_DATA_BIT_ERR |
		SD_EISEN_DATA_CRC_ERR | SD_EISEN_DATA_TMOUT_ERR |
		SD_EISEN_CMD_IDX_ERR | 	SD_EISEN_CMD_BIT_ERR |
		SD_EISEN_CMD_CRC_ERR | 	SD_EISEN_CMD_TMOUT_ERR;

	ambarella_sd_enable_irq(host, (eis << 16) | nis);
}

static void ambarella_sd_timer_timeout(unsigned long param)
{
	struct ambarella_mmc_host *host = (struct ambarella_mmc_host *)param;
	struct mmc_request *mrq = host->mrq;
	u32 dir;

	dev_err(host->dev, "pending mrq: %s[%u]\n",
			mrq ? mrq->data ? "data" : "cmd" : "",
			mrq ? mrq->cmd->opcode : 9999);

	if (mrq) {
		if (mrq->data) {
			if (mrq->data->flags & MMC_DATA_WRITE)
				dir = DMA_TO_DEVICE;
			else
				dir = DMA_FROM_DEVICE;
			dma_unmap_sg(host->dev, mrq->data->sg, mrq->data->sg_len, dir);
		}
		mrq->cmd->error = -ETIMEDOUT;
		ambarella_sd_release_bus(host->mmc);
		host->mrq = NULL;
		host->cmd = NULL;
		mmc_request_done(host->mmc, mrq);
	}

	ambarella_sd_reset_all(host);
}

static void ambarella_sd_set_clk(struct mmc_host *mmc, u32 clock)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	u32 sd_clk, divisor, clk_reg;

	ambarella_sd_switch_clk(host, false);

	host->clock = 0;

	if (clock != 0) {
#if defined(CONFIG_AMBALINK_SD)
		/* sdmmc0 clk already set by RTOS */
		if (0 != strcmp(host->dev->of_node->name, "sdmmc0"))
			clk_set_rate(host->clk, max_t(u32, clock, 24000000));
#else
		clk_set_rate(host->clk, max_t(u32, clock, 24000000));
#endif
		sd_clk = clk_get_rate(host->clk);

		for (divisor = 1; divisor <= 256; divisor <<= 1) {
			if (sd_clk / divisor <= clock)
				break;
		}

		sd_clk /= divisor;

		host->clock = sd_clk;
		host->ns_in_one_cycle = DIV_ROUND_UP(1000000000, sd_clk);
		mmc->max_busy_timeout = (1 << 27) / (sd_clk / 1000);

#if defined(CONFIG_AMBALINK_SD)
{
		struct rpdev_sdinfo *sdinfo = ambarella_sd_sdinfo_get(mmc);
		u32 orig_clk_reg;

		clk_reg = ((divisor << 7) & 0xff00) | SD_CLK_ICLK_EN;

		if (sdinfo->is_init && sdinfo->from_rpmsg) {
			orig_clk_reg = readw_relaxed(host->regbase + SD_CLK_OFFSET);
		} else {
			orig_clk_reg = clk_reg = ((divisor << 7) & 0xff00) | SD_CLK_ICLK_EN;
		}

		if ((orig_clk_reg & 0xff00) != (clk_reg & 0xff00)) {
			dev_info(host->dev,
				"internal clock divisor is mismatched (RTOS = 0x%08x, Linux = 0x%08x)\n",
				orig_clk_reg, clk_reg);
		}
}
#else
		/* convert divisor to register setting: (divisor >> 1) << 8 */
		clk_reg = ((divisor << 7) & 0xff00) | SD_CLK_ICLK_EN;
#endif
		writew_relaxed(clk_reg, host->regbase + SD_CLK_OFFSET);
		while (!(readw_relaxed(host->regbase + SD_CLK_OFFSET) & SD_CLK_ICLK_STABLE))
			cpu_relax();

		ambarella_sd_switch_clk(host, true);
	}
}

static void ambarella_sd_set_pwr(struct ambarella_mmc_host *host, u8 power_mode)
{
	if (power_mode == MMC_POWER_OFF) {
		ambarella_sd_reset_all(host);
		writeb_relaxed(SD_PWR_OFF, host->regbase + SD_PWR_OFFSET);

		if (gpio_is_valid(host->pwr_gpio)) {
			gpio_set_value_cansleep(host->pwr_gpio,
						!host->pwr_gpio_active);
			msleep(300);
		}

		if (gpio_is_valid(host->v18_gpio)) {
			gpio_set_value_cansleep(host->v18_gpio,
						!host->v18_gpio_active);
			msleep(10);
		}
	} else if (power_mode == MMC_POWER_UP) {
		if (gpio_is_valid(host->v18_gpio)) {
			gpio_set_value_cansleep(host->v18_gpio,
						!host->v18_gpio_active);
			msleep(10);
		}

		if (gpio_is_valid(host->pwr_gpio)) {
			gpio_set_value_cansleep(host->pwr_gpio,
						host->pwr_gpio_active);
			msleep(300);
		}

		writeb_relaxed(SD_PWR_ON | SD_PWR_3_3V, host->regbase + SD_PWR_OFFSET);
	}

	host->power_mode = power_mode;
}

static void ambarella_sd_set_bus(struct ambarella_mmc_host *host, u8 bus_width, u8 mode)
{
	u32 hostr = 0, tmp;

	hostr = readb_relaxed(host->regbase + SD_HOST_OFFSET);
	hostr |= SD_HOST_ADMA;

	switch (bus_width) {
	case MMC_BUS_WIDTH_8:
		hostr |= SD_HOST_8BIT;
		hostr &= ~(SD_HOST_4BIT);
		break;
	case MMC_BUS_WIDTH_4:
		hostr &= ~(SD_HOST_8BIT);
		hostr |= SD_HOST_4BIT;
		break;
	case MMC_BUS_WIDTH_1:
	default:
		hostr &= ~(SD_HOST_8BIT);
		hostr &= ~(SD_HOST_4BIT);
		break;
	}

	switch (mode) {
	case MMC_TIMING_UHS_DDR50:
		tmp = readl_relaxed(host->regbase + SD_XC_CTR_OFFSET);
		tmp |= SD_XC_CTR_DDR_EN;
		writel_relaxed(tmp, host->regbase + SD_XC_CTR_OFFSET);

		tmp = readw_relaxed(host->regbase + SD_HOST2_OFFSET);
		tmp |= 0x0004;
		writew_relaxed(tmp, host->regbase + SD_HOST2_OFFSET);

		hostr |= SD_HOST_HIGH_SPEED;
		writeb_relaxed(hostr, host->regbase + SD_HOST_OFFSET);
		break;
	default:
		tmp = readl_relaxed(host->regbase + SD_XC_CTR_OFFSET);
		tmp &= ~SD_XC_CTR_DDR_EN;
		writel_relaxed(tmp, host->regbase + SD_XC_CTR_OFFSET);

		tmp = readw_relaxed(host->regbase + SD_HOST2_OFFSET);
		tmp &= ~0x0004;
		writew_relaxed(tmp, host->regbase + SD_HOST2_OFFSET);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
		if (force_sdio_host_high_speed == 1)
			hostr |= SD_HOST_HIGH_SPEED;
		else
			hostr &= ~SD_HOST_HIGH_SPEED;
#else
		hostr &= ~SD_HOST_HIGH_SPEED;
#endif
		writeb_relaxed(hostr, host->regbase + SD_HOST_OFFSET);
		break;
	}
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	if ((-1!=sdio_host_odly) || (-1!=sdio_host_ocly) || (-1!=sdio_host_idly)) {
		amba_sdio_delay_post_apply(sdio_host_odly, sdio_host_ocly, sdio_host_idly);
	}
#endif

	host->bus_width = bus_width;
	host->mode = mode;
}

static void ambarella_sd_recovery(struct ambarella_mmc_host *host)
{
	u32 latency, ctrl0_reg = 0, ctrl1_reg = 0, ctrl2_reg = 0;
	u32 divisor = 0;

	latency = readl_relaxed(host->regbase + SD_LAT_CTRL_OFFSET);
	if (host->phy_ctrl0_reg) {
		ctrl0_reg = readl_relaxed(host->phy_ctrl0_reg);
		ctrl1_reg = readl_relaxed(host->phy_ctrl1_reg);
		ctrl2_reg = readl_relaxed(host->phy_ctrl2_reg);
	}

	divisor = readl_relaxed(host->regbase + SD_CLK_OFFSET) & 0xff00;

	ambarella_sd_reset_all(host);

	/*restore the clock*/
	ambarella_sd_switch_clk(host, false);
	writew_relaxed((divisor | SD_CLK_ICLK_EN), host->regbase + SD_CLK_OFFSET);
	while (!(readw_relaxed(host->regbase + SD_CLK_OFFSET) & SD_CLK_ICLK_STABLE))
		cpu_relax();
	ambarella_sd_switch_clk(host, true);

	ambarella_sd_set_bus(host, host->bus_width, host->mode);

	writel_relaxed(latency, host->regbase + SD_LAT_CTRL_OFFSET);
	if (host->phy_ctrl0_reg) {
		writel_relaxed(ctrl2_reg, host->phy_ctrl2_reg);
		writel_relaxed(ctrl1_reg, host->phy_ctrl1_reg);
		writel_relaxed(ctrl0_reg | SD_PHY_RESET, host->phy_ctrl0_reg);
		udelay(5); /* DLL reset time */
		writel_relaxed(ctrl0_reg, host->phy_ctrl0_reg);
		udelay(5); /* DLL lock time */
	}
}

static void ambarella_sd_check_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);

#if defined(CONFIG_AMBALINK_SD)
	struct rpdev_sdinfo *sdinfo = ambarella_sd_sdinfo_get(mmc);
	if (sdinfo->from_rpmsg && !sdinfo->is_sdio) {
		ios->bus_width = (sdinfo->bus_width == 8) ? MMC_BUS_WIDTH_8 :
				 (sdinfo->bus_width == 4) ? MMC_BUS_WIDTH_4 :
				 	MMC_BUS_WIDTH_1;
		ios->clock = sdinfo->clk;
	}
#endif

	if (host->power_mode != ios->power_mode)
		ambarella_sd_set_pwr(host, ios->power_mode);

#if defined(CONFIG_AMBALINK_SD)
	if (((readl_relaxed(host->regbase + SD_CLK_OFFSET) & SD_CLK_EN) == 0) &&
		sdinfo->is_init && sdinfo->from_rpmsg) {
		/* RTOS will on/off clock every sd request,
		 * if clock is disable, that means RTOS has ever access sd
		 * controller and we need to enable clock again. */
		host->clock = 0;
	}
#endif

	if (host->clock != ios->clock)
		ambarella_sd_set_clk(mmc, ios->clock);

	if ((host->bus_width != ios->bus_width) || (host->mode != ios->timing))
		ambarella_sd_set_bus(host, ios->bus_width, ios->timing);
}

static void ambarella_sd_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	ambarella_sd_request_bus(mmc);

	ambarella_sd_check_ios(mmc, ios);

	ambarella_sd_release_bus(mmc);
}

static int ambarella_sd_check_cd(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	int cd_pin = host->fixed_cd;

	if (cd_pin < 0)
		cd_pin = mmc_gpio_get_cd(mmc);

	if (cd_pin < 0) {
		cd_pin = readl_relaxed(host->regbase + SD_STA_OFFSET);
		cd_pin &= SD_STA_CARD_INSERTED;
	}

	return !!cd_pin;
}

static int ambarella_sd_get_cd(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	int cd_pin = host->fixed_cd;

	ambarella_sd_request_bus(mmc);

	cd_pin = ambarella_sd_check_cd(mmc);

	ambarella_sd_release_bus(mmc);

	return cd_pin;
}

static int ambarella_sd_get_ro(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	int wp_pin = host->fixed_wp;

	ambarella_sd_request_bus(mmc);

	if (wp_pin < 0)
		wp_pin = mmc_gpio_get_ro(mmc);

	if (wp_pin < 0) {
		wp_pin = readl_relaxed(host->regbase + SD_STA_OFFSET);
		wp_pin &= SD_STA_WPS_PL;
	}

	ambarella_sd_release_bus(mmc);

	return !!wp_pin;
}

static int ambarella_sd_check_ready(struct ambarella_mmc_host *host)
{
	u32 sta_reg = 0, check = SD_STA_CMD_INHIBIT_CMD;
	unsigned long timeout;

	if (ambarella_sd_check_cd(host->mmc) == 0)
		return -ENOMEDIUM;

	if (host->cmd->data || (host->cmd->flags & MMC_RSP_BUSY))
		check |= SD_STA_CMD_INHIBIT_DAT;

	/* We shouldn't wait for data inhibit for stop commands, even
	   though they might use busy signaling */
	if (host->cmd == host->mrq->stop)
		check &= ~SD_STA_CMD_INHIBIT_DAT;

	timeout = jiffies + HZ;

	while ((readl_relaxed(host->regbase + SD_STA_OFFSET) & check)
		&& time_before(jiffies, timeout))
		cpu_relax();

	if (time_after(jiffies, timeout)) {
		dev_err(host->dev, "Wait bus idle timeout [0x%08x]\n", sta_reg);
		ambarella_sd_recovery(host);
		return -ETIMEDOUT;
	}

	return 0;
}

static u32 ambarella_sd_calc_timeout(struct ambarella_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;
	u32 clks, cycle_ns = host->ns_in_one_cycle;

	if (data)
		clks = data->timeout_clks + DIV_ROUND_UP(data->timeout_ns, cycle_ns);
	else
		clks = cmd->busy_timeout * 1000000 / host->ns_in_one_cycle;

	if (clks == 0)
		clks = 1 << 27;

	return clamp(order_base_2(clks), 13, 27) - 13;
}

static void ambarella_sd_setup_dma(struct ambarella_mmc_host *host,
		struct mmc_data *data)
{
	struct ambarella_sd_dma_desc *desc;
	struct scatterlist *sgent;
	u32 i, dir, sg_cnt, dma_len, dma_addr, word_num, byte_num;

	if (data->flags & MMC_DATA_WRITE)
		dir = DMA_TO_DEVICE;
	else
		dir = DMA_FROM_DEVICE;

	sg_cnt = dma_map_sg(host->dev, data->sg, data->sg_len, dir);
	BUG_ON(sg_cnt != data->sg_len || sg_cnt == 0);

	desc = host->desc_virt;
	for_each_sg(data->sg, sgent, sg_cnt, i) {
		dma_addr = sg_dma_address(sgent);
		dma_len = sg_dma_len(sgent);

		BUG_ON(dma_len > SD_ADMA_TBL_LINE_MAX_LEN);

		word_num = dma_len / 4;
		byte_num = dma_len % 4;

		desc->attr = SD_ADMA_TBL_ATTR_TRAN | SD_ADMA_TBL_ATTR_VALID;

		if (word_num > 0) {
			desc->attr |= SD_ADMA_TBL_ATTR_WORD;
			desc->len = word_num % 0x10000; /* 0 means 65536 words */
			desc->addr = dma_addr;

			if (byte_num > 0) {
				dma_addr += word_num << 2;
				desc++;
			}
		}

		if (byte_num > 0) {
			desc->attr = SD_ADMA_TBL_ATTR_TRAN | SD_ADMA_TBL_ATTR_VALID;
			desc->len = byte_num % 0x10000; /* 0 means 65536 bytes */
			desc->addr = dma_addr;
		}

		if(unlikely(i == sg_cnt - 1))
			desc->attr |= SD_ADMA_TBL_ATTR_END;
		else
			desc++;
	}

	dma_sync_single_for_device(host->dev, host->desc_phys,
		(desc - host->desc_virt + SD_ADMA_TBL_LINE_SIZE), DMA_TO_DEVICE);

}

static int ambarella_sd_send_cmd(struct ambarella_mmc_host *host, struct mmc_command *cmd)
{
	u32 tmo_reg, cmd_reg, xfr_reg = 0, arg_reg = cmd->arg;
	int rval;

	host->cmd = cmd;

	dev_dbg(host->dev, "Start %s[%u], arg = %u\n",
		cmd->data ? "data" : "cmd", cmd->opcode, cmd->arg);

	rval = ambarella_sd_check_ready(host);
	if (rval < 0) {
		struct mmc_request *mrq = host->mrq;
		ambarella_sd_release_bus(host->mmc);
		cmd->error = rval;
		host->mrq = NULL;
		host->cmd = NULL;
		mmc_request_done(host->mmc, mrq);
		return -EIO;
	}

	ambarella_sd_check_ios(host->mmc, &host->mmc->ios);

	tmo_reg = ambarella_sd_calc_timeout(host);

	cmd_reg = SD_CMD_IDX(cmd->opcode);

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_NONE:
		cmd_reg |= SD_CMD_RSP_NONE;
		break;
	case MMC_RSP_R1B:
		cmd_reg |= SD_CMD_RSP_48BUSY;
		break;
	case MMC_RSP_R2:
		cmd_reg |= SD_CMD_RSP_136;
		break;
	default:
		cmd_reg |= SD_CMD_RSP_48;
		break;
	}

	if (mmc_resp_type(cmd) & MMC_RSP_CRC)
		cmd_reg |= SD_CMD_CHKCRC;

	if (mmc_resp_type(cmd) & MMC_RSP_OPCODE)
		cmd_reg |= SD_CMD_CHKIDX;

	if (cmd->data) {
		cmd_reg |= SD_CMD_DATA;
		xfr_reg = SD_XFR_DMA_EN;

		/* if stream, we should clear SD_XFR_BLKCNT_EN to enable
		 * infinite transfer, but this is NOT supported by ADMA.
		 * however, fortunately, it seems no one use MMC_DATA_STREAM
		 * in mmc core. */

		if (mmc_op_multi(cmd->opcode) || cmd->data->blocks > 1)
			xfr_reg |= SD_XFR_MUL_SEL | SD_XFR_BLKCNT_EN;

		if (cmd->data->flags & MMC_DATA_READ)
			xfr_reg |= SD_XFR_CTH_SEL;

		/* if CMD23 is used, we should not send CMD12, unless any
		 * errors happened in read/write operation. So we disable
		 * auto_cmd12, but send CMD12 manually when necessary. */
		if (host->auto_cmd12 && !cmd->mrq->sbc && cmd->data->stop)
			xfr_reg |= SD_XFR_AC12_EN;

		writel_relaxed(host->desc_phys, host->regbase + SD_ADMA_ADDR_OFFSET);
		writew_relaxed(cmd->data->blksz, host->regbase + SD_BLK_SZ_OFFSET);
		writew_relaxed(cmd->data->blocks, host->regbase + SD_BLK_CNT_OFFSET);
	}

	writeb_relaxed(tmo_reg, host->regbase + SD_TMO_OFFSET);
	writew_relaxed(xfr_reg, host->regbase + SD_XFR_OFFSET);
	writel_relaxed(arg_reg, host->regbase + SD_ARG_OFFSET);
	writew_relaxed(cmd_reg, host->regbase + SD_CMD_OFFSET);

	mod_timer(&host->timer, jiffies + 5 * HZ);

	return 0;
}

static void ambarella_sd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;

	WARN_ON(host->mrq);

	ambarella_sd_request_bus(mmc);

	spin_lock_irqsave(&host->lock, flags);

	host->mrq = mrq;

	if (mrq->data)
		ambarella_sd_setup_dma(host, mrq->data);

	ambarella_sd_send_cmd(host, mrq->sbc ? mrq->sbc : mrq->cmd);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void ambarella_sd_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	if (enable)
		ambarella_sd_enable_irq(host, SD_NISEN_CARD);
	else
		ambarella_sd_disable_irq(host, SD_NISEN_CARD);
}

static int ambarella_sd_switch_voltage(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);

	ambarella_sd_request_bus(mmc);

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		writeb_relaxed(SD_PWR_ON | SD_PWR_3_3V, host->regbase + SD_PWR_OFFSET);

		if (gpio_is_valid(host->v18_gpio)) {
			gpio_set_value_cansleep(host->v18_gpio,
						!host->v18_gpio_active);
			msleep(host->switch_1v8_dly);
		}
	} else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180) {
		writeb_relaxed(SD_PWR_ON | SD_PWR_1_8V, host->regbase + SD_PWR_OFFSET);

		if (gpio_is_valid(host->v18_gpio)) {
			gpio_set_value_cansleep(host->v18_gpio,
						host->v18_gpio_active);
			msleep(host->switch_1v8_dly);
		}
	}

	ambarella_sd_release_bus(mmc);

	return 0;
}

static int ambarella_sd_card_busy(struct mmc_host *mmc)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	int retval = 0;

	ambarella_sd_request_bus(mmc);

	retval = !(readl_relaxed(host->regbase + SD_STA_OFFSET) & 0x1F00000);

	ambarella_sd_release_bus(mmc);

	return retval;
}

static int ambarella_sd_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct ambarella_mmc_host *host = mmc_priv(mmc);
	u32 doing_retune = mmc->doing_retune;
	u32 clock = host->clock;
	u32 tmp, misc, sel, lat, s = -1, e = 0, middle;
	u32 best_misc = 0, best_s = -1, best_e = 0;
	int dly, longest_range = 0, range = 0;

	if (!host->phy_ctrl0_reg)
		return 0;

#if defined(CONFIG_AMBALINK_SD)
	if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
		/* In eMMC boot, RTOS should already complete the tuning. */
		return 0;
	}
#endif

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
	/* If the detail delay is specified, use it instead of relying on tuning */
	if (host->detail_delay.data) {
		ambarella_sd_detail_delay(host);
		return 0;
	}
#endif

	ambarella_sd_request_bus(mmc);

retry:
	if (doing_retune) {
		clock -= 10000000;
		if (clock <= 50000000)
			return -ECANCELED;

		ambarella_sd_set_clk(mmc, clock);
		dev_dbg(host->dev, "doing return, clock = %d\n", host->clock);
	}

	for (misc = 0; misc < 4; misc++) {
		writel_relaxed(0x8001, host->phy_ctrl2_reg);

		tmp = readl_relaxed(host->phy_ctrl0_reg);
		tmp &= 0x0000ffff;
		tmp |= ((misc >> 1) & 0x1) << 19;
		writel_relaxed(tmp | SD_PHY_RESET, host->phy_ctrl0_reg);
		usleep_range(5, 10);	/* DLL reset time */
		writel_relaxed(tmp, host->phy_ctrl0_reg);
		usleep_range(5, 10);	/* DLL lock time */

		lat = ((misc >> 0) & 0x1) + 1;
		tmp = (lat << 12) | (lat << 8) | (lat << 4) | (lat << 0);
		writel_relaxed(tmp, host->regbase + SD_LAT_CTRL_OFFSET);

		for (dly = 0; dly < 256; dly++) {
			tmp = readl_relaxed(host->phy_ctrl2_reg);
			tmp &= 0xfffffff9;
			tmp |= (((dly >> 6) & 0x3) << 1);
			writel_relaxed(tmp, host->phy_ctrl2_reg);

			sel = dly % 64;
			if (sel < 0x20)
				sel = 63 - sel;
			else
				sel = sel - 32;

			tmp = (sel << 16) | (sel << 8) | (sel << 0);
			writel_relaxed(tmp, host->phy_ctrl1_reg);

			if (mmc_send_tuning(mmc, opcode, NULL) == 0) {
				/* Tuning is successful at this tuning point */
				if (s == -1)
					s = dly;
				e = dly;
				range++;
			} else {
				if (range > 0) {
					dev_dbg(host->dev,
						"tuning: misc[0x%x], count[%d](%d - %d)\n",
						misc, e - s + 1, s, e);
				}
				if (range > longest_range) {
					best_misc = misc;
					best_s = s;
					best_e = e;
					longest_range = range;
				}
				s = -1;
				e = range = 0;
			}
		}

		/* in case the last timings are all working */
		if (range > longest_range) {
			if (range > 0) {
				dev_dbg(host->dev,
					"tuning last: misc[0x%x], count[%d](%d - %d)\n",
					misc, e - s + 1, s, e);
			}
			best_misc = misc;
			best_s = s;
			best_e = e;
			longest_range = range;
		}

		s = -1;
		e = range = 0;
	}

	if (longest_range == 0) {
		if (clock > 50000000) {
			doing_retune = 1;
			goto retry;
		}
		ambarella_sd_release_bus(mmc);
		return -EIO;
	}

	middle = (best_s + best_e) / 2;

	tmp = (((middle >> 6) & 0x3) << 1) | 0x8001;
	writel_relaxed(tmp, host->phy_ctrl2_reg);

	sel = middle % 64;
	if (sel < 0x20)
		sel = 63 - sel;
	else
		sel = sel - 32;

	tmp = (sel << 16) | (sel << 8) | (sel << 0);
	writel_relaxed(tmp, host->phy_ctrl1_reg);

	tmp = readl_relaxed(host->phy_ctrl0_reg);
	tmp &= 0x0000ffff;
	tmp |= ((best_misc >> 1) & 0x1) << 19;
	writel_relaxed(tmp | SD_PHY_RESET, host->phy_ctrl0_reg);
	msleep(1);	/* DLL reset time */
	writel_relaxed(tmp, host->phy_ctrl0_reg);
	msleep(1);	/* DLL lock time */

	lat = ((best_misc >> 0) & 0x1) + 1;
	tmp = (lat << 12) | (lat << 8) | (lat << 4) | (lat << 0);
	writel_relaxed(tmp, host->regbase + SD_LAT_CTRL_OFFSET);

	ambarella_sd_release_bus(mmc);

	return 0;
}

static const struct mmc_host_ops ambarella_sd_host_ops = {
	.request = ambarella_sd_request,
	.set_ios = ambarella_sd_set_ios,
	.get_ro = ambarella_sd_get_ro,
	.get_cd = ambarella_sd_get_cd,
	.enable_sdio_irq = ambarella_sd_enable_sdio_irq,
	.start_signal_voltage_switch = ambarella_sd_switch_voltage,
	.card_busy = ambarella_sd_card_busy,
	.execute_tuning = ambarella_sd_execute_tuning,
};

static void ambarella_sd_handle_irq(struct ambarella_mmc_host *host)
{
	struct mmc_command *cmd = host->cmd;
	u32 resp0, resp1, resp2, resp3;
	u16 nis, eis, ac12es;

	if (cmd == NULL) {
		return;
	}

	nis = host->irq_status & 0xffff;
	eis = host->irq_status >> 16;
	ac12es = host->ac12es;

	if (nis & SD_NIS_ERROR) {
		if (eis & (SD_EIS_CMD_BIT_ERR | SD_EIS_CMD_CRC_ERR))
			cmd->error = -EILSEQ;
		else if (eis & SD_EIS_CMD_TMOUT_ERR)
			cmd->error = -ETIMEDOUT;
		else if ((eis & SD_EIS_DATA_TMOUT_ERR) && !cmd->data)
			/* for cmd without data, but needs to check busy */
			cmd->error = -ETIMEDOUT;
		else if (eis & SD_EIS_CMD_IDX_ERR)
			cmd->error = -EIO;

		if (cmd->data) {
			if (eis & (SD_EIS_DATA_BIT_ERR | SD_EIS_DATA_CRC_ERR))
				cmd->data->error = -EILSEQ;
			else if (eis & SD_EIS_DATA_TMOUT_ERR)
				cmd->data->error = -ETIMEDOUT;
			else if (eis & SD_EIS_ADMA_ERR)
				cmd->data->error = -EIO;
		}

		if (cmd->data && cmd->data->stop && (eis & SD_EIS_ACMD12_ERR)) {
			if (ac12es & (SD_AC12ES_CRC_ERROR | SD_AC12ES_END_BIT))
				cmd->data->stop->error = -EILSEQ;
			else if (ac12es & SD_AC12ES_TMOUT_ERROR)
				cmd->data->stop->error = -ETIMEDOUT;
			else if (ac12es & (SD_AC12ES_NOT_EXECED | SD_AC12ES_INDEX))
				cmd->data->stop->error = -EIO;
		}

		if (!cmd->error && (!cmd->data || !cmd->data->error)
			&& (!cmd->data || !cmd->data->stop || !cmd->data->stop->error)) {
			dev_warn(host->dev, "Miss error: 0x%04x, 0x%04x!\n", nis, eis);
			cmd->error = -EIO;
		}

		if (cmd->opcode != MMC_SEND_TUNING_BLOCK &&
			cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200) {
			dev_dbg(host->dev, "%s[%u] error: "
				"nis[0x%04x], eis[0x%04x], ac12es[0x%08x]!\n",
				cmd->data ? "data" : "cmd", cmd->opcode,
				nis, eis, ac12es);
		}

		goto finish;
	}

	if (nis & SD_NIS_CMD_DONE) {
		if (cmd->flags & MMC_RSP_136) {
			resp0 = readl_relaxed(host->regbase + SD_RSP0_OFFSET);
			resp1 = readl_relaxed(host->regbase + SD_RSP1_OFFSET);
			resp2 = readl_relaxed(host->regbase + SD_RSP2_OFFSET);
			resp3 = readl_relaxed(host->regbase + SD_RSP3_OFFSET);
			cmd->resp[0] = (resp3 << 8) | (resp2 >> 24);
			cmd->resp[1] = (resp2 << 8) | (resp1 >> 24);
			cmd->resp[2] = (resp1 << 8) | (resp0 >> 24);
			cmd->resp[3] = (resp0 << 8);
		} else {
			cmd->resp[0] = readl_relaxed(host->regbase + SD_RSP0_OFFSET);
		}

		/* if cmd without data needs to check busy, we have to
		 * wait for transfer_complete IRQ */
		if (!cmd->data && !(cmd->flags & MMC_RSP_BUSY))
			goto finish;
	}

	if (nis & SD_NIS_XFR_DONE) {
		if (cmd->data)
			cmd->data->bytes_xfered = cmd->data->blksz * cmd->data->blocks;
		goto finish;
	}

	return;

finish:
	tasklet_schedule(&host->finish_tasklet);

	return;

}

static void ambarella_sd_tasklet_finish(unsigned long param)
{
	struct ambarella_mmc_host *host = (struct ambarella_mmc_host *)param;
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd;
	unsigned long flags;
	u16 dir;

	if (cmd == NULL || mrq == NULL)
		return;

	spin_lock_irqsave(&host->lock, flags);

	dev_dbg(host->dev, "End %s[%u], arg = %u\n",
		cmd->data ? "data" : "cmd", cmd->opcode, cmd->arg);

	del_timer(&host->timer);

	if(cmd->error || (cmd->data && (cmd->data->error ||
		(cmd->data->stop && cmd->data->stop->error))))
		ambarella_sd_recovery(host);

	/* now we send READ/WRITE cmd if current cmd is CMD23 */
	if (cmd == mrq->sbc) {
		ambarella_sd_send_cmd(host, mrq->cmd);
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	if (cmd->data) {
		u32 dma_size;

		dma_size = cmd->data->blksz * cmd->data->blocks;
		dir = cmd->data->flags & MMC_DATA_WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE;

		dma_sync_single_for_cpu(host->dev, host->desc_phys, dma_size,
					 DMA_FROM_DEVICE);
		dma_unmap_sg(host->dev, cmd->data->sg, cmd->data->sg_len, dir);

		/* send the STOP cmd manually if auto_cmd12 is disabled and
		 * there is no preceded CMD23 for multi-read/write cmd */
		if (!host->auto_cmd12 && !mrq->sbc && cmd->data->stop) {
			ambarella_sd_send_cmd(host, cmd->data->stop);
			spin_unlock_irqrestore(&host->lock, flags);
			return;
		}
	}

	/* auto_cmd12 is disabled if mrq->sbc existed, which means the SD
	 * controller will not send CMD12 automatically. However, if any
	 * error happened in CMD18/CMD25 (read/write), we need to send
	 * CMD12 manually. */
	if ((cmd == mrq->cmd) && mrq->sbc && mrq->data->error && mrq->stop) {
		dev_warn(host->dev, "SBC|DATA cmd error, send STOP manually!\n");
		ambarella_sd_send_cmd(host, mrq->stop);
		spin_unlock_irqrestore(&host->lock, flags);
		return;
	}

	ambarella_sd_release_bus(host->mmc);

	host->cmd = NULL;
	host->mrq = NULL;

	spin_unlock_irqrestore(&host->lock, flags);
	mmc_request_done(host->mmc, mrq);
}

static irqreturn_t ambarella_sd_irq(int irq, void *devid)
{
	struct ambarella_mmc_host *host = devid;
	struct mmc_command *cmd = host->cmd;

	/* Read and clear the interrupt registers. Note: ac12es has to be
	 * read here to clear auto_cmd12 error irq. */
	spin_lock(&host->lock);
	host->ac12es = readl_relaxed(host->regbase + SD_AC12ES_OFFSET);
	host->irq_status = readl_relaxed(host->regbase + SD_NIS_OFFSET);
	writel_relaxed(host->irq_status, host->regbase + SD_NIS_OFFSET);

	if (cmd && cmd->opcode != MMC_SEND_TUNING_BLOCK &&
			cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200) {
		dev_dbg(host->dev, "irq_status = 0x%08x, ac12es = 0x%08x\n",
			host->irq_status, host->ac12es);
	}

	if (host->irq_status & SD_NIS_CARD)
		mmc_signal_sdio_irq(host->mmc);

	if ((host->fixed_cd == -1) &&
		(host->irq_status & (SD_NIS_REMOVAL | SD_NIS_INSERT))) {
		dev_dbg(host->dev, "0x%08x, card %s\n", host->irq_status,
			(host->irq_status & SD_NIS_INSERT) ? "Insert" : "Removed");
#if defined(CONFIG_AMBALINK_SD)
		host->insert = (host->irq_status & SD_NIS_INSERT) ? 1 : 0;
		schedule_delayed_work(&host->detect, msecs_to_jiffies(500));
#else
		mmc_detect_change(host->mmc, msecs_to_jiffies(500));
#endif
	}

	ambarella_sd_handle_irq(host);
	spin_unlock(&host->lock);

	return IRQ_HANDLED;
}

static int ambarella_sd_init_hw(struct ambarella_mmc_host *host)
{
	u32 gpio_init_flag;
	int rval;

	if (gpio_is_valid(host->pwr_gpio)) {
		if (host->pwr_gpio_active)
			gpio_init_flag = GPIOF_OUT_INIT_LOW;
		else
			gpio_init_flag = GPIOF_OUT_INIT_HIGH;

		rval = devm_gpio_request_one(host->dev, host->pwr_gpio,
					gpio_init_flag, "sd ext power");
		if (rval < 0) {
			dev_err(host->dev, "Failed to request pwr-gpios!\n");
			return 0;
		}
	}

	if (gpio_is_valid(host->v18_gpio)) {
		if (host->v18_gpio_active)
			gpio_init_flag = GPIOF_OUT_INIT_LOW;
		else
			gpio_init_flag = GPIOF_OUT_INIT_HIGH;

		rval = devm_gpio_request_one(host->dev, host->v18_gpio,
				gpio_init_flag, "sd 1v8 switch");
		if (rval < 0) {
			dev_err(host->dev, "Failed to request v18-gpios!\n");
			return 0;
		}
	}

	ambarella_sd_reset_all(host);

	return 0;
}

static int ambarella_sd_of_parse(struct ambarella_mmc_host *host)
{
	struct device_node *np = host->dev->of_node, *child;
	struct mmc_host *mmc = host->mmc;
	enum of_gpio_flags flags;
	int rval;
	u32 ocr_mask = 0;

	rval = mmc_of_parse(mmc);
	if (rval < 0)
		return rval;

	if (!of_property_read_bool(np, "amb,no-cap-erase"))
		mmc->caps |= MMC_CAP_ERASE;

	if (!of_property_read_bool(np, "amb,no-cap-cmd23"))
		mmc->caps |= MMC_CAP_CMD23;

	if (of_property_read_u32(np, "amb,switch-1v8-dly", &host->switch_1v8_dly) < 0)
		host->switch_1v8_dly = 100;

	if (of_property_read_u32(np, "amb,fixed-wp", &host->fixed_wp) < 0)
		host->fixed_wp = -1;

	if (of_property_read_u32(np, "amb,fixed-cd", &host->fixed_cd) < 0)
		host->fixed_cd = -1;

	mmc_of_parse_voltage(np, &ocr_mask);

	host->emmc_boot = of_property_read_bool(np, "amb,emmc-boot");
	host->auto_cmd12 = !of_property_read_bool(np, "amb,no-auto-cmd12");

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
	if (of_property_read_u32(np, "amb,detail-delay", &host->detail_delay.data) < 0)
		host->detail_delay.data = 0;
#endif

	/* old style of sd device tree defines slot subnode, these codes are
	 * just for compatibility. Note: we should remove this workaroud when
	 * none use slot subnode in future. */
	child = of_get_child_by_name(np, "slot");
	if (child)
		np = child;


	/* gpio for external power control */
	host->pwr_gpio = of_get_named_gpio_flags(np, "pwr-gpios", 0, &flags);
	host->pwr_gpio_active = !!(flags & OF_GPIO_ACTIVE_LOW);

	/* gpio for 3.3v/1.8v switch */
	host->v18_gpio = of_get_named_gpio_flags(np, "v18-gpios", 0, &flags);
	host->v18_gpio_active = !!(flags & OF_GPIO_ACTIVE_LOW);

	mmc->ops = &ambarella_sd_host_ops;
	mmc->max_blk_size = 1024; /* please check SD_CAP_OFFSET */
	mmc->max_blk_count = 65535;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_segs = PAGE_SIZE / 8;
	mmc->max_seg_size = SD_ADMA_TBL_LINE_MAX_LEN;

#if defined(CONFIG_AMBALINK_SD)
	/* sdmmc0 clk already set by RTOS */
	if (0 != strcmp(host->dev->of_node->name, "sdmmc0"))
		clk_set_rate(host->clk, mmc->f_max);
#else
	clk_set_rate(host->clk, mmc->f_max);
#endif
	mmc->f_max = clk_get_rate(host->clk);
	mmc->f_min = 24000000 / 256;
	mmc->max_busy_timeout = (1 << 27) / (mmc->f_max / 1000);


	mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ |
			MMC_CAP_ERASE | MMC_CAP_BUS_WIDTH_TEST |
			MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;
	mmc->caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25;

	if (mmc->f_max > 50000000)
		mmc->caps |= MMC_CAP_UHS_SDR50;

	if (mmc->f_max > 100000000)
		mmc->caps |= MMC_CAP_UHS_SDR104;

	if (ocr_mask)
		mmc->ocr_avail = ocr_mask;
	else {
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
		if (gpio_is_valid(host->v18_gpio)) {
			mmc->ocr_avail |= MMC_VDD_165_195 | MMC_CAP_1_8V_DDR;

		}
	}

	return 0;
}

static int ambarella_sd_get_resource(struct platform_device *pdev,
			struct ambarella_mmc_host *host)
{
	struct resource *mem;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL) {
		dev_err(&pdev->dev, "Get SD/MMC mem resource failed!\n");
		return -ENXIO;
	}

	host->regbase = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
	if (host->regbase == NULL) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return -ENOMEM;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (mem == NULL) {
		host->phy_ctrl0_reg = NULL;
	} else {
		host->phy_ctrl0_reg = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
		if (host->phy_ctrl0_reg == NULL) {
			dev_err(&pdev->dev, "devm_ioremap() failed for phy_ctrl0_reg\n");
			return -ENOMEM;
		}

		host->phy_ctrl1_reg = host->phy_ctrl0_reg + 4;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (mem == NULL) {
		host->phy_ctrl2_reg = host->phy_ctrl0_reg;
	} else {
		host->phy_ctrl2_reg = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
		if (host->phy_ctrl2_reg == NULL) {
			dev_err(&pdev->dev, "devm_ioremap() failed for phy_ctrl2_reg\n");
			return -ENOMEM;
		}
	}

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		dev_err(&pdev->dev, "Get SD/MMC irq resource failed!\n");
		return -ENXIO;
	}

	host->clk = clk_get(host->dev, NULL);
	if (IS_ERR(host->clk)) {
		dev_err(host->dev, "Get PLL failed!\n");
		return PTR_ERR(host->clk);
	}

	return 0;
}

static int pre_notified[3] = {0};
static int sd_suspended = 0;

static int ambarella_sd_system_event(struct notifier_block *nb,
	unsigned long val, void *data)
{
	struct ambarella_mmc_host *host;

	host = container_of(nb, struct ambarella_mmc_host, system_event);

	switch (val) {
	case AMBA_EVENT_PRE_CPUFREQ:
		if (!sd_suspended) {
			pr_debug("%s[0x%08x]: Pre Change\n", __func__, (u32)(u64)host->regbase);
			down(&host->system_event_sem);
                        if (!strcmp(host->dev->of_node->name, "sdmmc0")) {
                                pre_notified[0] = 1;
                        } else if (!strcmp(host->dev->of_node->name, "sdmmc1")) {
                                pre_notified[1] = 1;
                        } else {
                                pre_notified[2] = 1;
                        }
		}
		break;
	case AMBA_EVENT_POST_CPUFREQ:
		if (!sd_suspended) {
			/* Note: SDs have independent clock. */
			pr_debug("%s[0x%08x]: Post Change\n", __func__, (u32)(u64)host->regbase);
			if ((!strcmp(host->dev->of_node->name, "sdmmc0")) && pre_notified[0]) {
				pre_notified[0] = 0;
			} else if ((!strcmp(host->dev->of_node->name, "sdmmc1")) && pre_notified[1]) {
				pre_notified[1] = 0;
			} else if (pre_notified[2]) {
				pre_notified[2] = 0;
			}
			up(&host->system_event_sem);
		}
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

static int ambarella_sd_probe(struct platform_device *pdev)
{
	struct ambarella_mmc_host *host;
	struct mmc_host *mmc;
	int rval;

	mmc = mmc_alloc_host(sizeof(struct ambarella_mmc_host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Failed to alloc mmc host!\n");
		return -ENOMEM;
	}

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = &pdev->dev;
	spin_lock_init(&host->lock);
	sema_init(&host->system_event_sem, 1);

	tasklet_init(&host->finish_tasklet,
		ambarella_sd_tasklet_finish, (unsigned long)host);

	host->desc_virt = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
				&host->desc_phys, GFP_KERNEL);
	if (!host->desc_virt) {
		dev_err(&pdev->dev, "Can't alloc DMA memory");
		rval = -ENOMEM;
		goto out0;
	}

	setup_timer(&host->timer, ambarella_sd_timer_timeout, (unsigned long)host);

	rval = ambarella_sd_get_resource(pdev, host);
	if (rval < 0)
		goto out1;

	rval = ambarella_sd_of_parse(host);
	if (rval < 0)
		goto out1;

	ambarella_sd_request_bus(mmc);

	rval = ambarella_sd_init_hw(host);
	if (rval < 0)
		goto out1;

	rval = devm_request_irq(&pdev->dev, host->irq, ambarella_sd_irq,
				IRQF_SHARED | IRQF_TRIGGER_HIGH,
				dev_name(&pdev->dev), host);
	if (rval < 0) {
		dev_err(&pdev->dev, "Can't Request IRQ%u!\n", host->irq);
		goto out2;
	}
#if defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
	ambarella_sd_info_init(mmc);
#endif

#if defined(CONFIG_AMBALINK_SD)
{
	struct rpdev_sdinfo *sdinfo;

	ambarella_sd_rpmsg_sdinfo_init(mmc);
	sdinfo = ambarella_sd_sdinfo_get(mmc);
	ambarella_sd_rpmsg_sdinfo_en(mmc, sdinfo->is_init);

	/* Set clock back to RTOS desired. */
	if (sdinfo->is_init) {
		clk_set_rate(host->clk, sdinfo->clk);
	}

	INIT_DELAYED_WORK(&host->detect, ambarella_sd_cd_detect);
	disable_irq(host->irq);
	enable_irq(host->irq);
}
#endif

	ambarella_sd_release_bus(mmc);

	rval = mmc_add_host(mmc);
	if (rval < 0) {
		dev_err(&pdev->dev, "Can't add mmc host!\n");
		goto out1;
	}

	host->system_event.notifier_call = ambarella_sd_system_event;
	ambarella_register_event_notifier(&host->system_event);

	ambarella_sd_add_debugfs(host);

	platform_set_drvdata(pdev, host);

	dev_info(&pdev->dev, "Max frequency is %uHz\n", mmc->f_max);

#if defined(CONFIG_MMC_AMBARELLA_DELAY)
	if (host->detail_delay.data) {
		dev_info(&pdev->dev, "detail delay %#x\n",
			host->detail_delay.data);
		ambarella_sd_detail_delay(host);
	}
#endif

	return 0;

out2:
	mmc_remove_host(mmc);
out1:
	dma_free_coherent(&pdev->dev, PAGE_SIZE, host->desc_virt, host->desc_phys);
out0:
	mmc_free_host(mmc);
	return rval;
}

static int ambarella_sd_remove(struct platform_device *pdev)
{
	struct ambarella_mmc_host *host = platform_get_drvdata(pdev);

#if defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
	ambarella_sd_info_deinit(host->mmc);
#endif

	ambarella_sd_remove_debugfs(host);

	tasklet_kill(&host->finish_tasklet);

	del_timer_sync(&host->timer);

	mmc_remove_host(host->mmc);

	dma_free_coherent(&pdev->dev, PAGE_SIZE, host->desc_virt, host->desc_phys);

	mmc_free_host(host->mmc);

	dev_info(&pdev->dev, "Remove Ambarella SD/MMC Host Controller.\n");

	return 0;
}

#ifdef CONFIG_PM
static int ambarella_sd_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	struct ambarella_mmc_host *host = platform_get_drvdata(pdev);

	sd_suspended = 1;

	down(&host->system_event_sem);

	if(host->mmc->pm_caps & MMC_PM_KEEP_POWER) {
		ambarella_sd_disable_irq(host, SD_NISEN_CARD);
		host->sd_nisen = readw_relaxed(host->regbase + SD_NISEN_OFFSET);
		host->sd_eisen = readw_relaxed(host->regbase + SD_EISEN_OFFSET);
		host->sd_nixen = readw_relaxed(host->regbase + SD_NIXEN_OFFSET);
		host->sd_eixen = readw_relaxed(host->regbase + SD_EIXEN_OFFSET);
	}

	disable_irq(host->irq);

	return 0;

}

static int ambarella_sd_resume(struct platform_device *pdev)
{
	struct ambarella_mmc_host *host = platform_get_drvdata(pdev);

	up(&host->system_event_sem);

	ambarella_sd_request_bus(host->mmc);

	if (gpio_is_valid(host->pwr_gpio))
		gpio_direction_output(host->pwr_gpio, host->pwr_gpio_active);

	if(host->mmc->pm_caps & MMC_PM_KEEP_POWER) {
	        writew_relaxed(host->sd_nisen, host->regbase + SD_NISEN_OFFSET);
	        writew_relaxed(host->sd_eisen, host->regbase + SD_EISEN_OFFSET);
	        writew_relaxed(host->sd_nixen, host->regbase + SD_NIXEN_OFFSET);
	        writew_relaxed(host->sd_eixen, host->regbase + SD_EIXEN_OFFSET);
		host->mmc->caps |= MMC_CAP_NONREMOVABLE;
	        mdelay(10);
	        ambarella_sd_set_clk(host->mmc, host->clock);
		ambarella_sd_set_bus(host, host->bus_width, host->mode);
	        mdelay(10);
		ambarella_sd_enable_irq(host, SD_NISEN_CARD);
	} else {
		clk_set_rate(host->clk, host->mmc->f_max);
		ambarella_sd_reset_all(host);
	}

	enable_irq(host->irq);

	ambarella_sd_release_bus(host->mmc);

	sd_suspended = 0;

	return 0;
}
#endif

void ambarella_sd_shutdown (struct platform_device *pdev)
{
	struct ambarella_mmc_host *host = platform_get_drvdata(pdev);
	struct mmc_command cmd = {0};

	if (!host->emmc_boot)
		return;

	if((system_state == SYSTEM_RESTART) || (system_state == SYSTEM_HALT)) {
		mmc_claim_host(host->mmc);
		cmd.opcode = 0;
		cmd.arg = 0xf0f0f0f0;
		cmd.flags = MMC_RSP_NONE;
		mmc_wait_for_cmd(host->mmc, &cmd, 0);
	}

}

static const struct of_device_id ambarella_mmc_dt_ids[] = {
	{ .compatible = "ambarella,sdmmc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ambarella_mmc_dt_ids);

static struct platform_driver ambarella_sd_driver = {
	.probe		= ambarella_sd_probe,
	.remove		= ambarella_sd_remove,
	.shutdown	= ambarella_sd_shutdown,
#ifdef CONFIG_PM
	.suspend	= ambarella_sd_suspend,
	.resume		= ambarella_sd_resume,
#endif
	.driver		= {
		.name	= "ambarella-sd",
		.of_match_table = ambarella_mmc_dt_ids,
	},
};

static int __init ambarella_sd_init(void)
{
	int rval = 0;

	rval = platform_driver_register(&ambarella_sd_driver);
	if (rval)
		printk(KERN_ERR "%s: Register failed %d!\n", __func__, rval);

	return rval;
}

static void __exit ambarella_sd_exit(void)
{
	platform_driver_unregister(&ambarella_sd_driver);
}

fs_initcall(ambarella_sd_init);
module_exit(ambarella_sd_exit);

MODULE_DESCRIPTION("Ambarella Media Processor SD/MMC Host Controller");
MODULE_AUTHOR("Anthony Ginger, <hfjiang@ambarella.com>");
MODULE_LICENSE("GPL");

