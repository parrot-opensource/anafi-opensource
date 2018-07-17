/*
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
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <plat/iav_helper.h>

/*===========================================================================*/

void ambcache_clean_range(void *addr, unsigned int size)
{
#if defined(__aarch64__)
	__dma_map_area(addr, size, DMA_TO_DEVICE);
#else
	__sync_cache_range_w(addr, size);
#endif
}
EXPORT_SYMBOL(ambcache_clean_range);

void ambcache_inv_range(void *addr, unsigned int size)
{
#if defined(__aarch64__)
	__dma_map_area(addr, size, DMA_FROM_DEVICE);
#else
	__sync_cache_range_r(addr, size);
#endif
}
EXPORT_SYMBOL(ambcache_inv_range);

/*===========================================================================*/

unsigned long get_ambarella_iavmem_phys(void)
{
	struct device_node *np;
	struct property *prop;

	np = of_find_node_by_path("/iavmem");
	if (!np) {
		pr_err("%s: No np found for iavmem\n", __func__);
		return 0;
	}

	prop = of_find_property(np, "reg", NULL);
	if (!prop || !prop->value || prop->length < sizeof(u32)) {
		pr_err("%s: Invalid np for iavmem\n", __func__);
		return 0;
	}

	return be32_to_cpup((__be32 *)prop->value);
}
EXPORT_SYMBOL(get_ambarella_iavmem_phys);

unsigned int get_ambarella_iavmem_size(void)
{
	struct device_node *np;
	struct property *prop;

	np = of_find_node_by_path("/iavmem");
	if (!np) {
		pr_err("%s: No np found for iavmem\n", __func__);
		return 0;
	}

	prop = of_find_property(np, "reg", NULL);
	if (!prop || !prop->value || prop->length < 2 * sizeof(u32)) {
		pr_err("%s: Invalid np for iavmem\n", __func__);
		return 0;
	}

	return be32_to_cpup((__be32 *)prop->value + 1);
}
EXPORT_SYMBOL(get_ambarella_iavmem_size);

/*===========================================================================*/

