/*
 *  fs/partitions/ambptb.c
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

#include "check.h"
#include "ambptb.h"
#include <plat/ptb.h>
#include <linux/of.h>

//#define ambptb_prt

#ifdef ambptb_prt
#define ambptb_prt printk
#else
#define ambptb_prt(format, arg...) do {} while (0)
#endif

int ambptb_partition(struct parsed_partitions *state)
{
#if defined(CONFIG_AMBALINK_SD)
	u32 sect_size;
	char *part_label;
	int i, result = 0;
	struct device_node *ofpart_node;
	struct device_node *pp;

	if (strncmp(state->pp_buf, " mmcblk", 7)) {
		result = -1;
		goto ambptb_partition_exit;
	}

	sect_size = bdev_logical_block_size(state->bdev);

	ofpart_node = of_get_parent(of_find_node_by_name(NULL, "partition"));
	if(!ofpart_node) {
		pr_err("device node partition is not found!");
		return -1;
	}

	i = 0;
	for_each_child_of_node(ofpart_node,  pp) {
		const __be32 *reg;
		int len;
		int a_cells, s_cells;

		reg = of_get_property(pp, "reg", &len);
		if (!reg) {
			continue;
		}

		a_cells = of_n_addr_cells(pp);
		s_cells = of_n_size_cells(pp);
		if (len / 4 != a_cells + s_cells) {
			pr_debug("%s: partition %s error parsing reg property.\n",
				 __func__, pp->full_name);
			goto ambptb_partition_exit;
		}

		state->parts[i].from = of_read_number(reg, a_cells) / sect_size;
		state->parts[i].size = of_read_number(reg + a_cells, s_cells) / sect_size;

		part_label = (char *) of_get_property(pp, "label", &len);
		if (!part_label)
			part_label = (char *) of_get_property(pp, "name", &len);
		strlcat(state->pp_buf, part_label, PAGE_SIZE);
		strlcat(state->pp_buf, " ", PAGE_SIZE);

		printk(KERN_NOTICE "0x%012llx-0x%012llx : \"[p%d] %s\"\n",
			(unsigned long long)state->parts[i].from,
			(unsigned long long)(state->parts[i].from + state->parts[i].size),
			i, part_label);

		i++;
	}

	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	result = 1;
#else
	int i, val, slot = 1;
	unsigned char *data;
	Sector sect;
	u32 sect_size, sect_offset, sect_address, ptb_address;
	flpart_meta_t *ptb_meta;
	char ptb_tmp[1 + BDEVNAME_SIZE + 1];
	int result = 0;
	struct device_node * np;

	ambptb_prt("amb partition\n");

	sect_size = bdev_logical_block_size(state->bdev);
	sect_offset = (sizeof(ptb_header_t) + sizeof(flpart_table_t)) % sect_size;

	np = of_find_node_with_property(NULL, "amb,ptb_address");
	if(!np) {
		ambptb_prt("can not find amb, ptb_address\n");
		return -1;
	}

	val = of_property_read_u32(np, "amb,ptb_address", &ptb_address);
	if(val < 0) {
		ambptb_prt("read amb, ptb_address fail\n");
		return -1;
	}

	sect_address = (ptb_address * sect_size + sizeof(ptb_header_t) + sizeof(flpart_table_t)) / sect_size;
	data = read_part_sector(state, sect_address, &sect);
	if (!data) {
		result = -1;
		goto ambptb_partition_exit;
	}

	ptb_meta = (flpart_meta_t *)(data + sect_offset);
	ambptb_prt("%s: magic[0x%08X]\n", __func__, ptb_meta->magic);
	if (ptb_meta->magic == PTB_META_MAGIC3) {
		for (i = 0; i < PART_MAX; i++) {
			if (slot >= state->limit)
				break;

			if (((ptb_meta->part_info[i].dev & PART_DEV_EMMC) ==
				PART_DEV_EMMC) &&
				(ptb_meta->part_info[i].nblk)) {
				state->parts[slot].from =
					ptb_meta->part_info[i].sblk;
				state->parts[slot].size =
					ptb_meta->part_info[i].nblk;
				snprintf(ptb_tmp, sizeof(ptb_tmp), " %s",
					ptb_meta->part_info[i].name);
				strlcat(state->pp_buf, ptb_tmp, PAGE_SIZE);
				ambptb_prt("%s: %s [p%d]\n", __func__,
					ptb_meta->part_info[i].name, slot);
				slot++;
			}
		}
		strlcat(state->pp_buf, "\n", PAGE_SIZE);
		result = 1;
	} else if ((ptb_meta->magic == PTB_META_MAGIC) ||
		(ptb_meta->magic == PTB_META_MAGIC2)) {
		for (i = 0; i < PART_MAX; i++) {
			if (slot >= state->limit)
				break;
			if ((ptb_meta->part_info[i].dev == BOOT_DEV_SM) &&
				(ptb_meta->part_info[i].nblk)) {
				state->parts[slot].from =
					ptb_meta->part_info[i].sblk;
				state->parts[slot].size =
					ptb_meta->part_info[i].nblk;
				snprintf(ptb_tmp, sizeof(ptb_tmp), " %s",
					ptb_meta->part_info[i].name);
				strlcat(state->pp_buf, ptb_tmp, PAGE_SIZE);
				ambptb_prt("%s: %s [p%d]\n", __func__,
					ptb_meta->part_info[i].name, slot);
				slot++;
			}
		}
		strlcat(state->pp_buf, "\n", PAGE_SIZE);
		result = 1;
	}
	put_dev_sector(sect);
#endif

ambptb_partition_exit:
	return result;
}

