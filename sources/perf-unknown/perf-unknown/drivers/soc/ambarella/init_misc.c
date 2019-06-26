/*
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
#include <plat/ambalink_cfg.h>

struct ambalink_shared_memory_layout ambalink_shm_layout;
#endif

static struct proc_dir_entry *ambarella_proc_dir = NULL;

struct proc_dir_entry *get_ambarella_proc_dir(void)
{
	return ambarella_proc_dir;
}
EXPORT_SYMBOL(get_ambarella_proc_dir);

static int __init ambarella_init_root_proc(void)
{
	ambarella_proc_dir = proc_mkdir("ambarella", NULL);
	if (!ambarella_proc_dir) {
		pr_err("failed to create ambarella root proc dir\n");
		return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
static int __init ambarella_init_misc(void)
{
	ambarella_init_root_proc();

	ambalink_init_mem();

	return 0;
}

core_initcall(ambarella_init_misc);
#else
core_initcall(ambarella_init_root_proc);
#endif

