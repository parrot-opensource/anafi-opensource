/*
 * arch/arm/mach-ambarella/clk.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
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
#include <linux/list.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <asm/uaccess.h>
#include <plat/rct.h>

static const char *gclk_names[] = {
	"pll_out_core", "pll_out_sd", "gclk_cortex", "gclk_axi",
	"smp_twd", "gclk_ddr", "gclk_core", "gclk_ahb", "gclk_apb",
	"gclk_idsp", "gclk_so", "gclk_vo2", "gclk_vo", "gclk_uart",
	"gclk_uart0", "gclk_uart1", "gclk_uart2", "gclk_audio",
	"gclk_sdxc", "gclk_sdio", "gclk_sd", "gclk_ir", "gclk_adc",
	"gclk_ssi", "gclk_ssi2", "gclk_ssi3", "gclk_pwm", "gclk_motor"
};

static int ambarella_clock_proc_show(struct seq_file *m, void *v)
{
	struct clk *gclk;
	int i;

	seq_printf(m, "\nClock Information:\n");
	for (i = 0; i < ARRAY_SIZE(gclk_names); i++) {
		gclk = clk_get_sys(NULL, gclk_names[i]);
		if (IS_ERR(gclk))
			continue;

		seq_printf(m, "\t%s:\t%lu Hz\n",
			__clk_get_name(gclk), clk_get_rate(gclk));
	}

	return 0;
}

static ssize_t ambarella_clock_proc_write(struct file *file,
	const char __user *buffer, size_t count, loff_t *ppos)
{
	struct clk *gclk;
	char *buf, clk_name[32];
	int freq, rval = count;

	pr_warn("!!!DANGEROUS!!! You must know what you are doning!\n");

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, buffer, count)) {
		rval = -EFAULT;
		goto exit;
	}

	sscanf(buf, "%s %d", clk_name, &freq);

	gclk = clk_get_sys(NULL, clk_name);
	if (IS_ERR(gclk)) {
		pr_err("Invalid clk name\n");
		rval = -EINVAL;
		goto exit;
	}

	clk_set_rate(gclk, freq);

exit:
	kfree(buf);
	return rval;
}

static int ambarella_clock_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ambarella_clock_proc_show, PDE_DATA(inode));
}

static const struct file_operations proc_clock_fops = {
	.open = ambarella_clock_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = ambarella_clock_proc_write,
	.release = single_release,
};

static int __init ambarella_init_clk(void)
{
	proc_create_data("clock", S_IRUGO, get_ambarella_proc_dir(),
		&proc_clock_fops, NULL);

	return 0;
}

late_initcall(ambarella_init_clk);

