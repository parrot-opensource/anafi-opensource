
/*
 * Hibernation support specific for ARM
 *
 * Derived from work on ARM hibernation support by:
 *
 * Ubuntu project, hibernation support for mach-dove
 * Copyright (C) 2010 Nokia Corporation (Hiroshi Doyu)
 * Copyright (C) 2010 Texas Instruments, Inc. (Teerth Reddy et al.)
 *  https://lkml.org/lkml/2010/6/18/4
 *  https://lists.linux-foundation.org/pipermail/linux-pm/2010-June/027422.html
 *  https://patchwork.kernel.org/patch/96442/
 *
 * Copyright (C) 2006 Rafael J. Wysocki <rjw@sisk.pl>
 *
 * Copyright (C) 2015 Ambarella, Shanghai(Jorney Tu)
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <asm/system_misc.h>
#include <asm/idmap.h>
#include <asm/suspend.h>
#include <asm/memory.h>
#include <asm/sections.h>
#include <linux/mtd/mtd.h>

#define HIBERNATE_MTD_NAME  "swp"

static int mtd_page_offset = 0;

struct mtd_info *mtd_probe_dev(void)
{
	struct mtd_info *info = NULL;
	info = get_mtd_device_nm(HIBERNATE_MTD_NAME);

	if (IS_ERR(info)) {
		printk("SWP: mtd dev no found!\n");
		return NULL;
	} else {
		/* Makesure the swp partition has 32M at least */
		if(info->size < 0x2000000){
			printk("ERR: swp partition size is less than 32M\n");
			return NULL;
		}

		printk("MTD name: %s\n", 		info->name);
		printk("MTD size: 0x%llx\n", 	info->size);
		printk("MTD blocksize: 0x%x\n", info->erasesize);
		printk("MTD pagesize: 0x%x\n", 	info->writesize);
	}
	return info;
}


int hibernate_mtd_check(struct mtd_info *mtd, int ofs)
{

	int loff = ofs;
	int block = 0;

	while(mtd_block_isbad(mtd, loff) > 0){

		if(loff > mtd->size){
			printk("SWP: overflow mtd device ...\n");
			loff = 0;
			break;
		}

		printk("SWP: offset %d is a bad block\n" ,loff);

		block = loff / mtd->erasesize;
		loff = (block + 1) * mtd->erasesize;
	}
	return loff / PAGE_SIZE;
}


int hibernate_write_page(struct mtd_info *mtd, void *buf)
{

	int ret, retlen;
	int offset = 0;

	/* Default: The 1st 4k(one PAGE_SIZE) is empty in "swp" mtd partition */
	mtd_page_offset++;

#if 1 /* bad block checking is needed ? */
	offset = hibernate_mtd_check(mtd, mtd_page_offset * PAGE_SIZE);
#else
	offset = mtd_page_offset;
#endif

	if(offset == 0)
		return -EINVAL;

	ret = mtd_write(mtd, PAGE_SIZE * offset, PAGE_SIZE, &retlen, buf);
	if(ret < 0){
		printk("SWP: MTD write failed!\n");
		return -EFAULT;
	}

	mtd_page_offset = offset;
	return 0;
}

int hibernate_save_image(struct mtd_info *mtd, struct snapshot_handle *snapshot)
{

	int ret;
	int nr_pages = 0;

	while (1) {
		ret = snapshot_read_next(snapshot);
		if (ret <= 0)
			break;

		ret = hibernate_write_page(mtd, data_of(*snapshot));
		if (ret)
			break;

		nr_pages++;
	}

	if(!ret)
		printk("SWP: %d pages have been saved\n",  nr_pages);

	if(!nr_pages)
		ret = -EINVAL;

	return ret;
}

int hibernate_mtd_write(struct mtd_info *mtd)
{

	int error = 0;
	struct swsusp_info *header;
	struct snapshot_handle snapshot;

	memset(&snapshot, 0, sizeof(struct snapshot_handle));

	if(nr_meta_pages <= 0)
		return -EFAULT;

	error = snapshot_read_next(&snapshot);
	if (error < PAGE_SIZE) {
		if (error >= 0)
			error = -EFAULT;
		goto out_finish;
	}
	header = (struct swsusp_info *)data_of(snapshot);

	/* TODO: SWP partition space size check */
	if (header->pages * 0x1000 > mtd->size){
		printk("ERR: swp partition[0x%llx] has not enough space for the kernel snapshot[0x%lx]\n", mtd->size, header->pages * 0x1000);
			return -ENOMEM;
	}

	error = hibernate_write_page(mtd, header);
	if (!error) {
		error = hibernate_save_image(mtd, &snapshot);
	}

out_finish:
	return error;

}

int swsusp_write_mtd(int flags)
{

	struct mtd_info *info = NULL;

	mtd_page_offset = 0;

	info = mtd_probe_dev();
	if(!info)
		return -EFAULT;

	return hibernate_mtd_write(info);
}

