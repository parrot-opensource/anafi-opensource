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
#include <linux/rational.h>

#define REF_CLK_FREQ			24000000

union ctrl_reg_u {
	struct {
		u32	write_enable		: 1;	/* [0] */
		u32	reserved1		: 1;	/* [1] */
		u32	bypass			: 1;	/* [2] */
		u32	frac_mode		: 1;	/* [3] */
		u32	force_reset		: 1;	/* [4] */
		u32	power_down		: 1;	/* [5] */
		u32	halt_vco		: 1;	/* [6] */
		u32	tristate		: 1;	/* [7] */
		u32	tout_async		: 4;	/* [11:8] */
		u32	sdiv			: 4;	/* [15:12] */
		u32	sout			: 4;	/* [19:16] */
		u32	force_lock		: 1;	/* [20] */
		u32	force_bypass		: 1;	/* [21] */
		u32	reserved2		: 2;	/* [23:22] */
		u32	intp			: 7;	/* [30:24] */
		u32	reserved3		: 1;	/* [31] */
	} s;
	u32	w;
};

union frac_reg_u {
	struct {
		u32	frac			: 31;	/* [30:0] */
		u32	nega			: 1;	/* [31] */
	} s;
	u32	w;
};

struct amb_clk_pll {
	struct clk_hw hw;
	void __iomem *ctrl_reg;
	void __iomem *frac_reg;
	void __iomem *ctrl2_reg;
	void __iomem *ctrl3_reg;
	void __iomem *pres_reg;
	void __iomem *post_reg;
	u32 extra_pre_scaler : 1;
	u32 extra_post_scaler : 1;
	u32 frac_mode : 1;
	u32 ctrl2_val;
	u32 ctrl3_val;
	u32 fix_divider;
	u32 min_vco; /* in MHz */
};

#define to_amb_clk_pll(_hw) container_of(_hw, struct amb_clk_pll, hw)
#define rct_writel_en(v, p)		\
		do {writel(v, p); writel((v | 0x1), p); writel(v, p);} while (0)

static unsigned long ambarella_pll_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct amb_clk_pll *clk_pll = to_amb_clk_pll(hw);
	u32 pre_scaler, post_scaler, intp, sdiv, sout;
	u64 dividend, divider, frac;
	union ctrl_reg_u ctrl_val;
	union frac_reg_u frac_val;

	ctrl_val.w = readl(clk_pll->ctrl_reg);
	if ((ctrl_val.s.power_down == 1) || (ctrl_val.s.halt_vco == 1))
		return 0;

	frac_val.w = readl(clk_pll->frac_reg);

	if (clk_pll->pres_reg != NULL) {
		pre_scaler = readl(clk_pll->pres_reg);
		if (clk_pll->extra_pre_scaler) {
			pre_scaler >>= 4;
			pre_scaler++;
		}
	} else {
		pre_scaler = 1;
	}

	if (clk_pll->post_reg != NULL) {
		post_scaler = readl(clk_pll->post_reg);
		if (clk_pll->extra_post_scaler) {
			post_scaler >>= 4;
			post_scaler++;
		}
	} else {
		post_scaler = 1;
	}

	if (ctrl_val.s.bypass || ctrl_val.s.force_bypass)
		return parent_rate / pre_scaler / post_scaler;

	intp = ctrl_val.s.intp + 1;
	sdiv = ctrl_val.s.sdiv + 1;
	sout = ctrl_val.s.sout + 1;

	dividend = parent_rate;
	dividend *= (u64)intp;
	dividend *= (u64)sdiv;
	if (ctrl_val.s.frac_mode) {
		if (frac_val.s.nega) {
			/* Negative */
			frac = 0x80000000 - frac_val.s.frac;
			frac = parent_rate * frac * sdiv;
			frac >>= 32;
			dividend = dividend - frac;
		} else {
			/* Positive */
			frac = frac_val.s.frac;
			frac = parent_rate * frac * sdiv;
			frac >>= 32;
			dividend = dividend + frac;
		}
	}

	divider = pre_scaler * sout * post_scaler * clk_pll->fix_divider;
	BUG_ON(divider == 0);

	do_div(dividend, divider);

	return dividend;
}

static long ambarella_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *parent_rate)
{
	struct amb_clk_pll *clk_pll = to_amb_clk_pll(hw);
	unsigned long round_rate;

	if (clk_pll->frac_mode)
		round_rate = rate;
	else
		round_rate = roundup(rate, REF_CLK_FREQ / 2);

	return round_rate;
}

static int ambarella_pll_set_rate(struct clk_hw *hw, unsigned long rate,
			unsigned long parent_rate)
{
	struct amb_clk_pll *clk_pll = to_amb_clk_pll(hw);
	unsigned long rate_tmp, pre_scaler = 1, post_scaler = 1;
	unsigned long intp, sdiv = 1, sout = 1;
	u64 dividend, divider, diff;
	union ctrl_reg_u ctrl_val;
	union frac_reg_u frac_val;

	rate *= clk_pll->fix_divider;

	if (rate < parent_rate && clk_pll->post_reg != NULL) {
		rate *= 16;
		post_scaler = 16;
	}

	if (rate < parent_rate) {
		pr_err("Error: target rate is too slow: %ld!\n", rate);
		return -EINVAL;
	}

	/* non-HDMI fvco should be less than 1.5GHz and higher than 700MHz.
	 * HDMI fvco should be less than 5.5GHz, and much higher than 700MHz,
	 * probably higher than 2GHz, but not sure, need VLSI's confirmation */

	rational_best_approximation(rate, parent_rate, 64, 16, &intp, &sout);

	while (parent_rate / 1000000 * intp * sdiv / pre_scaler < clk_pll->min_vco) {
		if (sout > 8 || intp > 64)
			break;
		intp *= 2;
		sout *= 2;
	}

	BUG_ON(intp > 128 || sout > 16 || sdiv > 16);
	BUG_ON(pre_scaler > 16 || post_scaler > 16);

	if (clk_pll->pres_reg != NULL) {
		if (clk_pll->extra_pre_scaler == 1)
			rct_writel_en((pre_scaler - 1) << 4, clk_pll->pres_reg);
		else
			writel(pre_scaler, clk_pll->pres_reg);
	}

	if (clk_pll->post_reg != NULL) {
		if (clk_pll->extra_post_scaler == 1)
			rct_writel_en((post_scaler - 1) << 4, clk_pll->post_reg);
		else
			writel(post_scaler, clk_pll->post_reg);
	}

	ctrl_val.w = readl(clk_pll->ctrl_reg);
	if(ctrl_val.s.frac_mode) {
		ctrl_val.s.force_reset = 1;
		rct_writel_en(ctrl_val.w, clk_pll->ctrl_reg);
	}

	ctrl_val.s.intp = intp - 1;
	ctrl_val.s.sdiv = sdiv - 1;
	ctrl_val.s.sout = sout - 1;
	ctrl_val.s.bypass = 0;
	ctrl_val.s.frac_mode = 0;
	ctrl_val.s.force_reset = 0;
	ctrl_val.s.power_down = 0;
	ctrl_val.s.halt_vco = 0;
	ctrl_val.s.tristate = 0;
	ctrl_val.s.force_lock = 1;
	ctrl_val.s.force_bypass = 0;
	ctrl_val.s.write_enable = 0;
	rct_writel_en(ctrl_val.w, clk_pll->ctrl_reg);

	if (clk_pll->frac_mode) {
		rate_tmp = ambarella_pll_recalc_rate(hw, parent_rate);
		rate_tmp *= clk_pll->fix_divider * post_scaler;
		if (rate_tmp <= rate)
			diff = rate - rate_tmp;
		else
			diff = rate_tmp - rate;

		if (diff) {
			dividend = diff * pre_scaler * sout;
			dividend = dividend << 32;
			divider = (u64)sdiv * parent_rate;
			dividend = DIV_ROUND_CLOSEST_ULL(dividend, divider);
			if (rate_tmp <= rate) {
				frac_val.s.nega	= 0;
				frac_val.s.frac	= dividend;
			} else {
				frac_val.s.nega	= 1;
				frac_val.s.frac	= 0x80000000 - dividend;
			}
			writel(frac_val.w, clk_pll->frac_reg);

			ctrl_val.s.frac_mode = 1;
		}

		rct_writel_en(ctrl_val.w, clk_pll->ctrl_reg);
	}

	writel(clk_pll->ctrl2_val, clk_pll->ctrl2_reg);
	if (ctrl_val.s.frac_mode)
		writel(clk_pll->ctrl3_val | (1 << 12), clk_pll->ctrl3_reg);
	else
		writel(clk_pll->ctrl3_val, clk_pll->ctrl3_reg);

	/* check if result rate is precise or not */
	rate_tmp = ambarella_pll_recalc_rate(hw, parent_rate);
	if (abs(rate_tmp - rate / clk_pll->fix_divider / post_scaler) > 10) {
		pr_warn("%s: rate is not very precise: %ld, %ld\n",
			clk_hw_get_name(hw), rate_tmp,
			rate / clk_pll->fix_divider / post_scaler);
	}

	return 0;
}

static const struct clk_ops pll_ops = {
	.recalc_rate = ambarella_pll_recalc_rate,
	.round_rate = ambarella_pll_round_rate,
	.set_rate = ambarella_pll_set_rate,
};

static inline void ambarella_pll_get_reg(struct device_node *np,
		struct amb_clk_pll *clk_pll)
{
	clk_pll->ctrl_reg = of_iomap(np, 0);
	if (!clk_pll->ctrl_reg) {
		pr_err("%s: failed to map ctrl_reg", np->name);
		return;
	}

	clk_pll->frac_reg = of_iomap(np, 1);
	if (!clk_pll->frac_reg) {
		pr_err("%s: failed to map frac_reg", np->name);
		return;
	}

	clk_pll->ctrl2_reg = of_iomap(np, 2);
	if (!clk_pll->ctrl2_reg) {
		pr_err("%s: failed to map ctrl2_reg", np->name);
		return;
	}

	clk_pll->ctrl3_reg = of_iomap(np, 3);
	if (!clk_pll->ctrl3_reg) {
		pr_err("%s: failed to map ctrl3_reg", np->name);
		return;
	}

	/* pre and post scaler registers are allowed non-exist */
	clk_pll->pres_reg = of_iomap(np, 4);
	clk_pll->post_reg = of_iomap(np, 5);
}

static void __init ambarella_pll_clocks_init(struct device_node *np)
{
	struct amb_clk_pll *clk_pll;
	struct clk *clk;
	struct clk_init_data init;
	const char *name, *parent_name;
	int num_parents;

	num_parents = of_clk_get_parent_count(np);
	if (num_parents < 1) {
		pr_err("%s: no parent found\n", np->name);
		return;
	}

	clk_pll = kzalloc(sizeof(struct amb_clk_pll), GFP_KERNEL);
	if (!clk_pll)
		return;

	ambarella_pll_get_reg(np, clk_pll);

	if (of_property_read_string(np, "clock-output-names", &name))
		name = np->name;

	parent_name = of_clk_get_parent_name(np, 0);

	clk_pll->extra_pre_scaler = !!of_find_property(np, "amb,extra-pre-scaler", NULL);
	clk_pll->extra_post_scaler = !!of_find_property(np, "amb,extra-post-scaler", NULL);
	clk_pll->frac_mode = !!of_find_property(np, "amb,frac-mode", NULL);

	if (of_property_read_u32(np, "amb,ctrl2-val", &clk_pll->ctrl2_val))
		clk_pll->ctrl2_val = 0x3f770000;

	if (of_property_read_u32(np, "amb,ctrl3-val", &clk_pll->ctrl3_val))
		clk_pll->ctrl3_val = 0x00068300;

	if (of_property_read_u32(np, "amb,fix-divider", &clk_pll->fix_divider))
		clk_pll->fix_divider = 1;

	if (of_property_read_u32(np, "amb,min-vco", &clk_pll->min_vco))
		clk_pll->min_vco = 700;

	init.name = name;
	init.ops = &pll_ops;
	init.flags = CLK_IS_BASIC | CLK_GET_RATE_NOCACHE;
	init.parent_names = &parent_name;
	init.num_parents = num_parents;
	clk_pll->hw.init = &init;

	clk = clk_register(NULL, &clk_pll->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: failed to register %s pll clock (%ld)\n",
		       __func__, name, PTR_ERR(clk));
		kfree(clk_pll);
		return;
	}

	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, name, NULL);
}
CLK_OF_DECLARE(ambarella_clk_pll, "ambarella,pll-clock", ambarella_pll_clocks_init);

