/*
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/of_address.h>

static struct clk_mux * __init ambarella_mux_clock_init(struct device_node *np)
{
	struct clk_mux *mux;
	void __iomem *mux_reg;
	u32 mux_mask, mux_shift, index = 0;

	if (of_device_is_compatible(np, "ambarella,div-clock"))
		return NULL;
	else if (of_device_is_compatible(np, "ambarella,composite-clock"))
		index = 1;

	mux_reg = of_iomap(np, index);
	if (!mux_reg) {
		pr_err("%s: failed to iomap reg for mux clock\n", np->name);
		return NULL;
	}

	/* sanity check */
	if (of_clk_get_parent_count(np) < 2) {
		pr_err("%s: mux clock needs 2 more parents\n", np->name);
		return NULL;
	}

	mux = kzalloc(sizeof(*mux), GFP_KERNEL);
	if (mux == NULL) {
		pr_err("%s: no memory for mux\n", np->name);
		return NULL;
	}

	if (of_property_read_u32(np, "amb,mux-mask", &mux_mask))
		mux_mask = 0x3;

	if (of_property_read_u32(np, "amb,mux-shift", &mux_shift))
		mux_shift = 0;

	mux->reg = mux_reg;
	mux->shift = mux_shift;
	mux->mask = mux_mask;

	return mux;
}

static struct clk_divider * __init ambarella_div_clock_init(struct device_node *np)
{
	struct clk_divider *div;
	void __iomem *div_reg;
	u32 div_width, div_shift;

	if (of_device_is_compatible(np, "ambarella,mux-clock"))
		return NULL;

	div_reg = of_iomap(np, 0);
	if (!div_reg) {
		pr_err("%s: failed to iomap reg for div clock\n", np->name);
		return NULL;
	}

	div = kzalloc(sizeof(*div), GFP_KERNEL);
	if (div == NULL) {
		pr_err("%s: no memory for div\n", np->name);
		return NULL;
	}

	if (of_property_read_u32(np, "amb,div-width", &div_width))
		div_width = 24;

	if (of_property_read_u32(np, "amb,div-shift", &div_shift))
		div_shift = 0;

	if(!!of_find_property(np, "amb,div-plus", NULL))
		div->flags = 0;
	else
		div->flags = CLK_DIVIDER_ONE_BASED;

	div->reg = div_reg;
	div->shift = div_shift;
	div->width = div_width;

	return div;
}

static void __init ambarella_composite_clocks_init(struct device_node *np)
{
	struct clk *clk;
	struct clk_mux *mux;
	struct clk_divider *div;
	const char *name, **parent_names;
	u32 num_parents;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents < 1) {
		pr_err("%s: no parent found\n", np->name);
		return;
	}

	parent_names = kzalloc(sizeof(char *) * num_parents, GFP_KERNEL);
	if (!parent_names) {
		pr_err("%s: no memory for parent_names\n", np->name);
		return;
	}

	of_clk_parent_fill(np, parent_names, num_parents);

	if (of_property_read_string(np, "clock-output-names", &name))
		name = np->name;

	div = ambarella_div_clock_init(np);
	mux = ambarella_mux_clock_init(np);

	clk = clk_register_composite(NULL, name, parent_names, num_parents,
			mux ? &mux->hw : NULL, &clk_mux_ops,
			div ? &div->hw : NULL, &clk_divider_ops,
			NULL, NULL,
			CLK_GET_RATE_NOCACHE | CLK_SET_RATE_NO_REPARENT);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register %s composite clock (%ld)\n",
		       __func__, name, PTR_ERR(clk));
		return;
	}

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, name, NULL);

	kfree(parent_names);
}

CLK_OF_DECLARE(ambarella_clk_composite,
		"ambarella,composite-clock", ambarella_composite_clocks_init);
CLK_OF_DECLARE(ambarella_clk_div,
		"ambarella,div-clock", ambarella_composite_clocks_init);
CLK_OF_DECLARE(ambarella_clk_mux,
		"ambarella,mux-clock", ambarella_composite_clocks_init);

