/*
 * arch/arm/plat-ambarella/generic/timer.c
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * Copyright (C) 2014-2018, Ambarella, Inc.
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
 * Default clock is from APB.
 */

#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/sched_clock.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <plat/timer.h>
#include <plat/event.h>

#define AMBARELLA_TIMER_FREQ		clk_get_rate(clk_get(NULL, "gclk_apb"))
#define AMBARELLA_TIMER_RATING		(300)

static void __iomem *sched_base_reg;

struct ambarella_timer_pm_reg {
	u32 clk_rate;
	u32 ctrl_reg;
	u32 status_reg;
	u32 reload_reg;
	u32 match1_reg;
	u32 match2_reg;
};

struct ambarella_clkevt {
	struct clock_event_device clkevt;
	void __iomem *base_reg;
	void __iomem *ctrl_reg;
	u32 ctrl_offset;
	int irq;
	char name[16];
	struct ambarella_timer_pm_reg pm_reg;
};

struct ambarella_clksrc {
	struct clocksource clksrc;
	void __iomem *base_reg;
	void __iomem *ctrl_reg;
	u32 ctrl_offset;
	struct ambarella_timer_pm_reg pm_reg;
};

static inline struct ambarella_clkevt *to_ambarella_clkevt(struct clock_event_device *evt)
{
	return container_of(evt, struct ambarella_clkevt, clkevt);
}

static inline struct ambarella_clksrc *to_ambarella_clksrc(struct clocksource *src)
{
	return container_of(src, struct ambarella_clksrc, clksrc);
}

/* ==========================================================================*/

static void ambarella_timer_suspend(struct ambarella_clkevt *amb_clkevt,
		struct ambarella_clksrc *amb_clksrc)
{
	struct ambarella_timer_pm_reg *pm_reg;
	void __iomem *regbase;
	void __iomem *ctrl_reg;
	u32 ctrl_offset;

	pm_reg = amb_clkevt ? &amb_clkevt->pm_reg: &amb_clksrc->pm_reg;
	regbase = amb_clkevt ? amb_clkevt->base_reg : amb_clksrc->base_reg;
	ctrl_reg = amb_clkevt ? amb_clkevt->ctrl_reg : amb_clksrc->ctrl_reg;
	ctrl_offset = amb_clkevt ? amb_clkevt->ctrl_offset : amb_clksrc->ctrl_offset;

	pm_reg->clk_rate = AMBARELLA_TIMER_FREQ;
	pm_reg->ctrl_reg = (readl_relaxed(ctrl_reg) >> ctrl_offset) & 0xf;
	pm_reg->status_reg = readl_relaxed(regbase + TIMER_STATUS_OFFSET);
	pm_reg->reload_reg = readl_relaxed(regbase + TIMER_RELOAD_OFFSET);
	pm_reg->match1_reg = readl_relaxed(regbase + TIMER_MATCH1_OFFSET);
	pm_reg->match2_reg = readl_relaxed(regbase + TIMER_MATCH2_OFFSET);

	atomic_io_modify(ctrl_reg, 0xf << ctrl_offset, 0);
}

static void ambarella_timer_resume(struct ambarella_clkevt *amb_clkevt,
		struct ambarella_clksrc *amb_clksrc)
{
	struct ambarella_timer_pm_reg *pm_reg;
	void __iomem *regbase;
	void __iomem *ctrl_reg;
	u32 ctrl_offset, clk_rate;

	pm_reg = amb_clkevt ? &amb_clkevt->pm_reg: &amb_clksrc->pm_reg;
	regbase = amb_clkevt ? amb_clkevt->base_reg : amb_clksrc->base_reg;
	ctrl_reg = amb_clkevt ? amb_clkevt->ctrl_reg : amb_clksrc->ctrl_reg;
	ctrl_offset = amb_clkevt ? amb_clkevt->ctrl_offset : amb_clksrc->ctrl_offset;

	atomic_io_modify(ctrl_reg, 0xf << ctrl_offset, 0);

	writel_relaxed(pm_reg->status_reg, regbase + TIMER_STATUS_OFFSET);
	writel_relaxed(pm_reg->reload_reg, regbase + TIMER_RELOAD_OFFSET);
	writel_relaxed(pm_reg->match1_reg, regbase + TIMER_MATCH1_OFFSET);
	writel_relaxed(pm_reg->match2_reg, regbase + TIMER_MATCH2_OFFSET);

	clk_rate = AMBARELLA_TIMER_FREQ;
	if (pm_reg->clk_rate == clk_rate)
		goto resume_exit;

	pm_reg->clk_rate = clk_rate;

	if (amb_clkevt) {
		clockevents_update_freq(&amb_clkevt->clkevt, clk_rate);
	} else {
		clocksource_change_rating(&amb_clksrc->clksrc, 0);
		__clocksource_update_freq_hz(&amb_clksrc->clksrc, clk_rate);
		clocksource_change_rating(&amb_clksrc->clksrc, AMBARELLA_TIMER_RATING);
	}

resume_exit:
	atomic_io_modify(ctrl_reg, 0xf << ctrl_offset, pm_reg->ctrl_reg << ctrl_offset);
}

/* ==========================================================================*/

static cycle_t ambarella_clksrc_timer_read(struct clocksource *clksrc)
{
	struct ambarella_clksrc *amb_clksrc = to_ambarella_clksrc(clksrc);
	return (-(u32)readl_relaxed(amb_clksrc->base_reg + TIMER_STATUS_OFFSET));
}

static void ambarella_clksrc_timer_suspend(struct clocksource *clksrc)
{
	struct ambarella_clksrc *amb_clksrc = to_ambarella_clksrc(clksrc);
	ambarella_timer_suspend(NULL, amb_clksrc);
}

static void ambarella_clksrc_timer_resume(struct clocksource *clksrc)
{
	struct ambarella_clksrc *amb_clksrc = to_ambarella_clksrc(clksrc);
	ambarella_timer_resume(NULL, amb_clksrc);
}

static u64 notrace ambarella_read_sched_clock(void)
{
	return (-(u64)readl_relaxed(sched_base_reg + TIMER_STATUS_OFFSET));
}

static int __init ambarella_clocksource_init(struct device_node *np)
{
	struct ambarella_clksrc *amb_clksrc;
	struct clocksource *clksrc;
	int rval;

	amb_clksrc = kzalloc(sizeof(struct ambarella_clksrc), GFP_KERNEL);
	if (!amb_clksrc) {
		pr_err("%s: No memory for ambarella_clksrc\n", __func__);
		return -ENOMEM;
	}

	amb_clksrc->base_reg = of_iomap(np, 0);
	if (!amb_clksrc->base_reg) {
		pr_err("%s: Failed to map source base\n", __func__);
		return -ENXIO;
	}

	amb_clksrc->ctrl_reg = of_iomap(np, 1);
	if (!amb_clksrc->ctrl_reg) {
		pr_err("%s: Failed to map timer-ctrl base\n", __func__);
		return -ENXIO;
	}

	rval = of_property_read_u32(np, "ctrl-offset", &amb_clksrc->ctrl_offset);
	if (rval < 0) {
		pr_err("%s: Can't get ctrl offset\n", __func__);
		return rval;
	}

	of_node_put(np);

	atomic_io_modify(amb_clksrc->ctrl_reg, 0xf << amb_clksrc->ctrl_offset, 0);
	writel_relaxed(0xffffffff, amb_clksrc->base_reg + TIMER_STATUS_OFFSET);
	writel_relaxed(0xffffffff, amb_clksrc->base_reg + TIMER_RELOAD_OFFSET);
	writel_relaxed(0x0, amb_clksrc->base_reg + TIMER_MATCH1_OFFSET);
	writel_relaxed(0x0, amb_clksrc->base_reg + TIMER_MATCH2_OFFSET);
	atomic_io_modify(amb_clksrc->ctrl_reg, 0xf << amb_clksrc->ctrl_offset,
			TIMER_CTRL_EN << amb_clksrc->ctrl_offset);

	clksrc = &amb_clksrc->clksrc;
	clksrc->name = "ambarella-cs-timer";
	clksrc->rating = AMBARELLA_TIMER_RATING;
	clksrc->flags = CLOCK_SOURCE_IS_CONTINUOUS,
	clksrc->mask = CLOCKSOURCE_MASK(32),
	clksrc->read = ambarella_clksrc_timer_read,
	clksrc->suspend = ambarella_clksrc_timer_suspend,
	clksrc->resume = ambarella_clksrc_timer_resume,

	clocksource_register_hz(clksrc, AMBARELLA_TIMER_FREQ);

	sched_base_reg = amb_clksrc->base_reg;
	sched_clock_register(ambarella_read_sched_clock, 32, AMBARELLA_TIMER_FREQ);

	return 0;
}

/* ==========================================================================*/

static int ambarella_clkevt_set_state_shutdown(struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);
	atomic_io_modify(amb_clkevt->ctrl_reg, TIMER_CTRL_EN << amb_clkevt->ctrl_offset, 0);
	return 0;
}

static int ambarella_clkevt_set_state_periodic(struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);
	u32 cnt = AMBARELLA_TIMER_FREQ / HZ;

	atomic_io_modify(amb_clkevt->ctrl_reg, 0xf << amb_clkevt->ctrl_offset, 0);

	writel_relaxed(cnt, amb_clkevt->base_reg + TIMER_STATUS_OFFSET);
	writel_relaxed(cnt, amb_clkevt->base_reg + TIMER_RELOAD_OFFSET);
	writel_relaxed(0x0, amb_clkevt->base_reg + TIMER_MATCH1_OFFSET);
	writel_relaxed(0x0, amb_clkevt->base_reg + TIMER_MATCH2_OFFSET);

	atomic_io_modify(amb_clkevt->ctrl_reg, 0xf << amb_clkevt->ctrl_offset,
		(TIMER_CTRL_OF | TIMER_CTRL_EN) << amb_clkevt->ctrl_offset);

	return 0;
}

static int ambarella_clkevt_set_state_oneshot(struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);

	atomic_io_modify(amb_clkevt->ctrl_reg, 0xf << amb_clkevt->ctrl_offset, 0);

	writel_relaxed(0x0, amb_clkevt->base_reg + TIMER_STATUS_OFFSET);
	writel_relaxed(0xffffffff, amb_clkevt->base_reg + TIMER_RELOAD_OFFSET);
	writel_relaxed(0x0, amb_clkevt->base_reg + TIMER_MATCH1_OFFSET);
	writel_relaxed(0x0, amb_clkevt->base_reg + TIMER_MATCH2_OFFSET);

	atomic_io_modify(amb_clkevt->ctrl_reg, 0xf << amb_clkevt->ctrl_offset,
		(TIMER_CTRL_OF | TIMER_CTRL_EN) << amb_clkevt->ctrl_offset);

	return 0;
}

static int ambarella_clkevt_set_next_event(unsigned long delta,
	struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);
	writel_relaxed(delta, amb_clkevt->base_reg + TIMER_STATUS_OFFSET);
	return 0;
}

static void ambarella_clkevt_suspend(struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);
	ambarella_timer_suspend(amb_clkevt, NULL);
}

static void ambarella_clkevt_resume(struct clock_event_device *clkevt)
{
	struct ambarella_clkevt *amb_clkevt = to_ambarella_clkevt(clkevt);
	ambarella_timer_resume(amb_clkevt, NULL);
}

static irqreturn_t ambarella_clkevt_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *clkevt = dev_id;

	clkevt->event_handler(clkevt);
	return IRQ_HANDLED;
}

static int __init ambarella_clockevent_init(struct device_node *np)
{
	struct ambarella_clkevt *amb_clkevt;
	struct clock_event_device *clkevt;
	int rval;

	amb_clkevt = kzalloc(sizeof(struct ambarella_clkevt), GFP_KERNEL);
	if (!amb_clkevt) {
		pr_err("%s: No memory for ambarella_clkevt\n", __func__);
		return -ENOMEM;
	}

	amb_clkevt->base_reg = of_iomap(np, 0);
	if (!amb_clkevt->base_reg) {
		pr_err("%s: Failed to map event base\n", __func__);
		return -ENXIO;
	}

	amb_clkevt->ctrl_reg = of_iomap(np, 1);
	if (!amb_clkevt->ctrl_reg) {
		pr_err("%s: Failed to map timer-ctrl base\n", __func__);
		return -ENXIO;
	}

	amb_clkevt->irq = irq_of_parse_and_map(np, 0);
	if (amb_clkevt->irq <= 0) {
		pr_err("%s: Can't get irq\n", __func__);
		return -EINVAL;
	}

	rval = of_property_read_u32(np, "ctrl-offset", &amb_clkevt->ctrl_offset);
	if (rval < 0) {
		pr_err("%s: Can't get ctrl offset\n", __func__);
		return rval;
	}

	sprintf(amb_clkevt->name, "ambarella-ce-timer");

	of_node_put(np);

	clkevt = &amb_clkevt->clkevt;
	clkevt->name = amb_clkevt->name;
	clkevt->irq = amb_clkevt->irq;
	clkevt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	clkevt->rating = AMBARELLA_TIMER_RATING;
	clkevt->cpumask = cpumask_of(0);
	clkevt->set_next_event = ambarella_clkevt_set_next_event;
	clkevt->set_state_periodic = ambarella_clkevt_set_state_periodic,
	clkevt->set_state_oneshot = ambarella_clkevt_set_state_oneshot;
	clkevt->set_state_shutdown = ambarella_clkevt_set_state_shutdown;
	clkevt->tick_resume = ambarella_clkevt_set_state_shutdown;
	clkevt->suspend = ambarella_clkevt_suspend;
	clkevt->resume = ambarella_clkevt_resume;

	rval = request_irq(clkevt->irq, ambarella_clkevt_interrupt,
			IRQF_TIMER | IRQF_TRIGGER_RISING,
			clkevt->name, clkevt);
	if (rval < 0) {
		pr_err("%s: request_irq failed\n", __func__);
		return rval;
	}

	clockevents_config_and_register(&amb_clkevt->clkevt,
				AMBARELLA_TIMER_FREQ, 1, 0xffffffff);

	return 0;
}

/* ==========================================================================*/

static DEFINE_PER_CPU(struct ambarella_clkevt, percpu_clkevt);

static int ambarella_local_timer_starting_cpu(unsigned int cpu)
{
	struct ambarella_clkevt *local_clkevt = per_cpu_ptr(&percpu_clkevt, cpu);
	struct clock_event_device *clkevt = &local_clkevt->clkevt;

	clkevt->name = local_clkevt->name;
	clkevt->irq = local_clkevt->irq;
	clkevt->cpumask = cpumask_of(cpu);
	clkevt->features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	clkevt->rating = AMBARELLA_TIMER_RATING + 30;
	clkevt->set_next_event = ambarella_clkevt_set_next_event;
	clkevt->set_state_periodic = ambarella_clkevt_set_state_periodic;
	clkevt->set_state_oneshot = ambarella_clkevt_set_state_oneshot;
	clkevt->set_state_shutdown = ambarella_clkevt_set_state_shutdown;
	clkevt->tick_resume = ambarella_clkevt_set_state_shutdown;
	clkevt->suspend = ambarella_clkevt_suspend;
	clkevt->resume = ambarella_clkevt_resume;

	clockevents_config_and_register(clkevt, AMBARELLA_TIMER_FREQ, 1, 0xffffffff);

	irq_force_affinity(local_clkevt->irq, cpumask_of(cpu));
	enable_irq(clkevt->irq);

	return 0;
}

static int ambarella_local_timer_dying_cpu(unsigned int cpu)
{
	struct ambarella_clkevt *local_clkevt = per_cpu_ptr(&percpu_clkevt, cpu);
	struct clock_event_device *clkevt = &local_clkevt->clkevt;

	clkevt->set_state_shutdown(clkevt);
	disable_irq(clkevt->irq);

	return 0;
}

static int __init ambarella_local_clockevent_init(struct device_node *np)
{
	struct ambarella_clkevt *local_clkevt;
	int cpu, rval;

	for_each_possible_cpu(cpu) {
		local_clkevt = per_cpu_ptr(&percpu_clkevt, cpu);

		local_clkevt->base_reg = of_iomap(np, cpu * 2);
		if (!local_clkevt->base_reg) {
			pr_err("%s: Failed to map event base[%d]\n", __func__, cpu);
			return -ENXIO;
		}

		local_clkevt->ctrl_reg = of_iomap(np, cpu * 2 + 1);
		if (!local_clkevt->ctrl_reg) {
			pr_err("%s: Failed to map ctrl reg[%d]\n", __func__, cpu);
			return -ENXIO;
		}

		rval = of_property_read_u32_index(np, "ctrl-offset",
					cpu, &local_clkevt->ctrl_offset);
		if (rval < 0) {
			pr_err("%s: Can't get local ctrl offset[%d]\n", __func__, cpu);
			return rval;
		}

		local_clkevt->irq = irq_of_parse_and_map(np, cpu);
		if (local_clkevt->irq <= 0) {
			pr_err("%s: Can't get irq[%d]\n", __func__, cpu);
			return -EINVAL;
		}

		sprintf(local_clkevt->name, "local_clkevt%d", cpu);

		irq_set_status_flags(local_clkevt->irq, IRQ_NOAUTOEN);

		rval = request_irq(local_clkevt->irq, ambarella_clkevt_interrupt,
				IRQF_TIMER | IRQF_NOBALANCING | IRQF_TRIGGER_RISING,
				local_clkevt->name, &local_clkevt->clkevt);
		if (rval < 0) {
			pr_err("%s: request_irq failed[%d]\n", __func__, cpu);
			return rval;
		}
	}

	of_node_put(np);

	/* Install hotplug callbacks which configure the timer on this CPU */
	rval = cpuhp_setup_state(CPUHP_AP_ARM_ARCH_TIMER_STARTING,
				"AP_AMBARELLA_TIMER_STARTING",
				ambarella_local_timer_starting_cpu,
				ambarella_local_timer_dying_cpu);
	if (rval)
		return rval;

	return 0;
}

CLOCKSOURCE_OF_DECLARE(ambarella_cs,
		"ambarella,clock-source", ambarella_clocksource_init);
CLOCKSOURCE_OF_DECLARE(ambarella_ce,
		"ambarella,clock-event", ambarella_clockevent_init);
CLOCKSOURCE_OF_DECLARE(ambarella_local_ce,
		"ambarella,local-clock-event", ambarella_local_clockevent_init);

