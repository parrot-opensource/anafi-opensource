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

#include <linux/init.h>
#include <linux/kernel.h>

#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>

#include <plat/ambalink_cfg.h>

enum {
	AMBARELLA_IO_DESC_PPM_ID = 0,
	AMBARELLA_IO_DESC_PPM2_ID,
};

struct ambarella_mem_map_desc {
	char		name[8];
	unsigned long	virtual;
	unsigned long	physical;
	unsigned long	length;
};

static struct ambarella_mem_map_desc ambarella_io_desc[] = {
	[AMBARELLA_IO_DESC_PPM_ID] = {
		.name		= "PPM",    /* Private Physical Memory (shared memory) */
		.virtual	= 0,
		.physical	= 0,
		.length		= 0,
	},
	[AMBARELLA_IO_DESC_PPM2_ID] = {
		.name		= "PPM2",   /* Private Physical Memory (RTOS memory) */
		.virtual	= 0,
		.physical	= 0,
		.length		= 0,
	},
};

#if 0
unsigned long ambarella_phys_to_virt(unsigned long paddr)
{
	int i;
	unsigned long phystart, phylength, phyoffset, vstart;

	for (i = 0; i < ARRAY_SIZE(ambarella_io_desc); i++) {
		phystart = ambarella_io_desc[i].physical;
		phylength = ambarella_io_desc[i].length;
		vstart = ambarella_io_desc[i].virtual;
		if ((paddr >= phystart) && (paddr < (phystart + phylength))) {
			phyoffset = paddr - phystart;
			return (unsigned long)(vstart + phyoffset);
		}
	}

	return __raw_phys_to_virt(paddr);
}
EXPORT_SYMBOL(ambarella_phys_to_virt);

unsigned long ambarella_virt_to_phys(unsigned long vaddr)
{
	int i;
	unsigned long phystart, vlength, voffset, vstart;

	for (i = 0; i < ARRAY_SIZE(ambarella_io_desc); i++) {
		phystart = ambarella_io_desc[i].physical;
		vlength = ambarella_io_desc[i].length;
		vstart = ambarella_io_desc[i].virtual;
		if ((vaddr >= vstart) && (vaddr < (vstart + vlength))) {
			voffset = vaddr - vstart;
			return (unsigned long)(phystart + voffset);
		}
	}
	return __raw_virt_to_phys((void *) vaddr);
}
EXPORT_SYMBOL(ambarella_virt_to_phys);
#endif

void __init ambalink_init_mem(void)
{
	unsigned long base, size, virt;
	__be32 *reg;
	int len, err;
	struct device_node *memory_node;

	/* Create shared memory mapping. */
	memory_node = of_find_node_by_name(NULL, "shm");

	reg = (__be32 *) of_get_property(memory_node, "reg", &len);
	if (WARN_ON(!reg || (len != 2 * sizeof(u32))))
		return;

	base = be32_to_cpu(reg[0]);
	size = be32_to_cpu(reg[1]);

	memblock_add(base, size);

	virt = (u64) phys_to_virt(base);

	err = ioremap_page_range(virt, virt + size, base, __pgprot(PROT_NORMAL));
	if (err) {
		vunmap((void *)virt);
		pr_info("%s: ioremap_page_range failed: 0x%08lx - 0x%08lx (0x%016lx)\n",
			__func__, base, base + size,virt);
		return;
	}

	ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].virtual	= virt;
	ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].physical	= base;
	ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].length	= size;

	pr_info("ambalink shared memory : 0x%08lx - 0x%08lx (0x%016lx)\n",
		base, base + size, virt);

	ambalink_shm_layout.vring_c0_and_c1_buf         = base;
	ambalink_shm_layout.vring_c0_to_c1              =
		ambalink_shm_layout.vring_c0_and_c1_buf + RPMSG_TOTAL_BUF_SPACE;

	ambalink_shm_layout.vring_c1_to_c0              =
		ambalink_shm_layout.vring_c0_to_c1 + VRING_SIZE;
	ambalink_shm_layout.rpmsg_suspend_backup_addr   =
		ambalink_shm_layout.vring_c1_to_c0 + VRING_SIZE;

	ambalink_shm_layout.rpc_profile_addr            =
		ambalink_shm_layout.rpmsg_suspend_backup_addr + RPMSG_SUSPEND_BACKUP_SIZE;
	ambalink_shm_layout.rpmsg_profile_addr          =
		ambalink_shm_layout.rpc_profile_addr + RPC_PROFILE_SIZE;

	ambalink_shm_layout.aipc_slock_addr             =
		ambalink_shm_layout.rpmsg_profile_addr + MAX_RPC_RPMSG_PROFILE_SIZE;
	ambalink_shm_layout.aipc_mutex_addr             =
		ambalink_shm_layout.aipc_slock_addr + AIPC_SLOCK_SIZE;
}
EXPORT_SYMBOL(ambalink_init_mem);

