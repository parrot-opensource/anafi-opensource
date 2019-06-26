/*
 * drivers/pinctrl/ambarella/pinctrl-amb.c
 *
 * History:
 *	2013/12/18 - [Cao Rongrong] created file
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/irqchip/chained_irq.h>
#include <plat/gpio.h>

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
#include <plat/ambalink_cfg.h>
#include <linux/aipc/ipc_slock.h>
#endif

#define GPIO_MAX_BANK_NUM		8

struct amb_gpio_bank {
	void __iomem *regbase;
	int irq;
	cpumask_t affinity;
#if defined(CONFIG_PM)
	u32 irq_wake_mask;
	u32 data;
	u32 dir;
	u32 is;
	u32 ibe;
	u32 iev;
	u32 ie;
	u32 afsel;
	u32 mask;
#endif
};

struct amb_gpio_chip {
	void __iomem			*iomux_base;
	struct amb_gpio_bank		bank[GPIO_MAX_BANK_NUM];
	int				bank_num;
	struct gpio_chip		*gc;
	struct irq_domain		*domain;
	u32 banks_irq_mask;
};

#if defined(CONFIG_PM) && defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
#define linux_only_mask_gpio(regval, saved, mask)	((regval & ~mask) | (saved & mask))

static u32 ambalink_gpio_linux_only_mask[GPIO_MAX_BANK_NUM];
#endif

static void amb_gpio_raw_lock(unsigned long *pflags)
{
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	aipc_spin_lock_irqsave(AMBA_IPC_SPINLOCK_GPIO, pflags);
#endif
}

static void amb_gpio_raw_unlock(unsigned long *pflags)
{
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	aipc_spin_unlock_irqrestore(AMBA_IPC_SPINLOCK_GPIO, *pflags);
#endif
}

/* gpiolib gpio_request callback function */
static int amb_gpio_request(struct gpio_chip *gc, unsigned pin)
{
	return pinctrl_request_gpio(gc->base + pin);
}

/* gpiolib gpio_set callback function */
static void amb_gpio_free(struct gpio_chip *gc, unsigned pin)
{
	pinctrl_free_gpio(gc->base + pin);
}

/* gpiolib gpio_free callback function */
static void amb_gpio_set(struct gpio_chip *gc, unsigned pin, int value)
{
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(gc->parent);
	void __iomem *regbase;
	u32 bank, offset, mask;
	unsigned long flags;
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	u32 mask_bak;
#endif

	bank = PINID_TO_BANK(pin);
	offset = PINID_TO_OFFSET(pin);
	regbase = amb_gpio->bank[bank].regbase;
	mask = (0x1 << offset);

	amb_gpio_raw_lock(&flags);
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	mask_bak = readl_relaxed(regbase + GPIO_MASK_OFFSET);
#endif

	writel_relaxed(mask, regbase + GPIO_MASK_OFFSET);
	if (value == GPIO_LOW)
		mask = 0;
	writel_relaxed(mask, regbase + GPIO_DATA_OFFSET);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	writel_relaxed(mask_bak, regbase + GPIO_MASK_OFFSET);
#endif
	amb_gpio_raw_unlock(&flags);
}

/* gpiolib gpio_get callback function */
static int amb_gpio_get(struct gpio_chip *gc, unsigned pin)
{
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(gc->parent);
	void __iomem *regbase;
	u32 bank, offset, mask, data;
	unsigned long flags;
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	u32 mask_bak;
#endif

	bank = PINID_TO_BANK(pin);
	offset = PINID_TO_OFFSET(pin);
	regbase = amb_gpio->bank[bank].regbase;
	mask = (0x1 << offset);

	amb_gpio_raw_lock(&flags);
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	mask_bak = readl_relaxed(regbase + GPIO_MASK_OFFSET);
#endif
	writel_relaxed(mask, regbase + GPIO_MASK_OFFSET);
	data = readl_relaxed(regbase + GPIO_DATA_OFFSET);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	writel_relaxed(mask_bak, regbase + GPIO_MASK_OFFSET);
#endif
	amb_gpio_raw_unlock(&flags);

	data = (data >> offset) & 0x1;

	return (data ? GPIO_HIGH : GPIO_LOW);
}

/* gpiolib gpio_direction_input callback function */
static int amb_gpio_direction_input(struct gpio_chip *gc, unsigned pin)
{
	return pinctrl_gpio_direction_input(gc->base + pin);
}

/* gpiolib gpio_direction_output callback function */
static int amb_gpio_direction_output(struct gpio_chip *gc,
		unsigned pin, int value)
{
	int rval;

	rval = pinctrl_gpio_direction_output(gc->base + pin);
	if (rval < 0)
		return rval;

	amb_gpio_set(gc, pin, value);

	return 0;
}

/* gpiolib gpio_to_irq callback function */
static int amb_gpio_to_irq(struct gpio_chip *gc, unsigned pin)
{
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(gc->parent);

	return irq_create_mapping(amb_gpio->domain, pin);
}

static void amb_gpio_dbg_show(struct seq_file *s, struct gpio_chip *gc)
{
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(gc->parent);
	void __iomem *iomux_base = amb_gpio->iomux_base;
	void __iomem *regbase;
	u32 afsel = 0, data = 0, dir = 0, mask = 0;
	u32 iomux0 = 0, iomux1 = 0, iomux2 = 0, alt = 0;
	u32 i, bank, offset;
	unsigned long flags;
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
	u32 mask_bak;
#endif

	for (i = 0; i < gc->ngpio; i++) {
		offset = PINID_TO_OFFSET(i);
		if (offset == 0) {
			bank = PINID_TO_BANK(i);
			regbase = amb_gpio->bank[bank].regbase;

			amb_gpio_raw_lock(&flags);
#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
			mask_bak = readl_relaxed(regbase + GPIO_MASK_OFFSET);
#endif
			afsel = readl_relaxed(regbase + GPIO_AFSEL_OFFSET);
			dir = readl_relaxed(regbase + GPIO_DIR_OFFSET);
			mask = readl_relaxed(regbase + GPIO_MASK_OFFSET);
			writel_relaxed(~afsel, regbase + GPIO_MASK_OFFSET);
			data = readl_relaxed(regbase + GPIO_DATA_OFFSET);
			writel_relaxed(mask, regbase + GPIO_MASK_OFFSET);

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
			writel_relaxed(mask_bak, regbase + GPIO_MASK_OFFSET);
#endif
			amb_gpio_raw_unlock(&flags);

			seq_printf(s, "\nGPIO[%d]:\t[%d - %d]\n",
				bank, i, i + GPIO_BANK_SIZE - 1);
			seq_printf(s, "GPIO_AFSEL:\t0x%08X\n", afsel);
			seq_printf(s, "GPIO_DIR:\t0x%08X\n", dir);
			seq_printf(s, "GPIO_MASK:\t0x%08X:0x%08X\n", mask, ~afsel);
			seq_printf(s, "GPIO_DATA:\t0x%08X\n", data);

			if (iomux_base != NULL) {
				iomux0 = readl_relaxed(iomux_base + bank * 12);
				iomux1 = readl_relaxed(iomux_base + bank * 12 + 4);
				iomux2 = readl_relaxed(iomux_base + bank * 12 + 8);
				seq_printf(s, "IOMUX_REG%d_0:\t0x%08X\n", bank, iomux0);
				seq_printf(s, "IOMUX_REG%d_1:\t0x%08X\n", bank, iomux1);
				seq_printf(s, "IOMUX_REG%d_2:\t0x%08X\n", bank, iomux2);
			}
		}

		seq_printf(s, " gpio-%-3d", gc->base + i);
		if (iomux_base != NULL) {
			alt = ((iomux2 >> offset) & 1) << 2;
			alt |= ((iomux1 >> offset) & 1) << 1;
			alt |= ((iomux0 >> offset) & 1) << 0;
			if (alt != 0)
				seq_printf(s, " [HW  ] (alt%d)\n", alt);
			else {
				const char *label = gpiochip_is_requested(gc, i);
				label = label ? : "";
				seq_printf(s, " [GPIO] (%-20.20s) %s %s\n", label,
					(dir & (1 << offset)) ? "out" : "in ",
					(data & (1 << offset)) ? "hi" : "lo");
			}
		} else {
			if (afsel & (1 << offset)) {
				seq_printf(s, " [HW  ]\n");
			} else {
				const char *label = gpiochip_is_requested(gc, i);
				label = label ? : "";
				seq_printf(s, " [GPIO] (%-20.20s) %s %s\n", label,
					(dir & (1 << offset)) ? "out" : "in ",
					(data & (1 << offset)) ? "hi" : "lo");
			}
		}
	}
}

static struct gpio_chip amb_gc = {
	.label			= "ambarella-gpio",
	.base			= 0,
	.ngpio			= 0, /* assigned in probe */
	.request		= amb_gpio_request,
	.free			= amb_gpio_free,
	.direction_input	= amb_gpio_direction_input,
	.direction_output	= amb_gpio_direction_output,
	.get			= amb_gpio_get,
	.set			= amb_gpio_set,
	.to_irq			= amb_gpio_to_irq,
	.dbg_show		= amb_gpio_dbg_show,
	.owner			= THIS_MODULE,
};

static void amb_gpio_irq_enable(struct irq_data *data)
{
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(amb_gc.parent);
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	void __iomem *iomux_base = amb_gpio->iomux_base;
	u32 i, val, bank, offset;
	unsigned long flags;

	bank = PINID_TO_BANK(data->hwirq);
	offset = PINID_TO_OFFSET(data->hwirq);

	/* make sure the pin is in gpio mode */
	if (!gpiochip_is_requested(&amb_gc, data->hwirq)) {
		val = readl_relaxed(regbase + GPIO_AFSEL_OFFSET);
		val &= ~(0x1 << offset);
		writel_relaxed(val, regbase + GPIO_AFSEL_OFFSET);

		val = readl_relaxed(regbase + GPIO_DIR_OFFSET);
		val &= ~(0x1 << offset);
		writel_relaxed(val, regbase + GPIO_DIR_OFFSET);

		if (iomux_base) {
			for (i = 0; i < 3; i++) {
				val = readl_relaxed(iomux_base + IOMUX_REG_OFFSET(bank, i));
				val &= ~(0x1 << offset);
				writel_relaxed(val, iomux_base + IOMUX_REG_OFFSET(bank, i));
			}
			writel_relaxed(0x1, iomux_base + IOMUX_CTRL_SET_OFFSET);
			writel_relaxed(0x0, iomux_base + IOMUX_CTRL_SET_OFFSET);
		}
	}

	amb_gpio_raw_lock(&flags);

	writel_relaxed(0x1 << offset, regbase + GPIO_IC_OFFSET);

	val = readl_relaxed(regbase + GPIO_IE_OFFSET);
	val |= 0x1 << offset;
	writel_relaxed(val, regbase + GPIO_IE_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static void amb_gpio_irq_disable(struct irq_data *data)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	u32 ie = readl_relaxed(regbase + GPIO_IE_OFFSET);
	unsigned long flags;

	amb_gpio_raw_lock(&flags);

	writel_relaxed(ie & ~(0x1 << offset), regbase + GPIO_IE_OFFSET);
	writel_relaxed(0x1 << offset, regbase + GPIO_IC_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static void amb_gpio_irq_ack(struct irq_data *data)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	unsigned long flags;

	amb_gpio_raw_lock(&flags);

	writel_relaxed(0x1 << offset, regbase + GPIO_IC_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static void amb_gpio_irq_mask(struct irq_data *data)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	u32 ie = readl_relaxed(regbase + GPIO_IE_OFFSET);
	unsigned long flags;

	amb_gpio_raw_lock(&flags);

	writel_relaxed(ie & ~(0x1 << offset), regbase + GPIO_IE_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static void amb_gpio_irq_mask_ack(struct irq_data *data)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	u32 ie = readl_relaxed(regbase + GPIO_IE_OFFSET);
	unsigned long flags;

	amb_gpio_raw_lock(&flags);

	writel_relaxed(ie & ~(0x1 << offset), regbase + GPIO_IE_OFFSET);
	writel_relaxed(0x1 << offset, regbase + GPIO_IC_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static void amb_gpio_irq_unmask(struct irq_data *data)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	u32 ie = readl_relaxed(regbase + GPIO_IE_OFFSET);
	unsigned long flags;

	amb_gpio_raw_lock(&flags);

	writel_relaxed(ie | (0x1 << offset), regbase + GPIO_IE_OFFSET);

	amb_gpio_raw_unlock(&flags);
}

static int amb_gpio_irq_set_type(struct irq_data *data, unsigned int type)
{
	void __iomem *regbase = irq_data_get_irq_chip_data(data);
	struct irq_desc *desc = irq_to_desc(data->irq);
	u32 offset = PINID_TO_OFFSET(data->hwirq);
	u32 mask, bit, sense, bothedges, event;
	unsigned long flags;

	mask = ~(0x1 << offset);
	bit = (0x1 << offset);

	amb_gpio_raw_lock(&flags);

	sense = readl_relaxed(regbase + GPIO_IS_OFFSET);
	bothedges = readl_relaxed(regbase + GPIO_IBE_OFFSET);
	event = readl_relaxed(regbase + GPIO_IEV_OFFSET);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		sense &= mask;
		bothedges &= mask;
		event |= bit;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		sense &= mask;
		bothedges &= mask;
		event &= mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		sense &= mask;
		bothedges |= bit;
		event &= mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		sense |= bit;
		bothedges &= mask;
		event |= bit;
		desc->handle_irq = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		sense |= bit;
		bothedges &= mask;
		event &= mask;
		desc->handle_irq = handle_level_irq;
		break;
	default:
		pr_err("%s: irq[%d] type[%d] fail!\n",
			__func__, data->irq, type);
		return -EINVAL;
	}

	writel_relaxed(sense, regbase + GPIO_IS_OFFSET);
	writel_relaxed(bothedges, regbase + GPIO_IBE_OFFSET);
	writel_relaxed(event, regbase + GPIO_IEV_OFFSET);
	/* clear obsolete irq */
	writel_relaxed(0x1 << offset, regbase + GPIO_IC_OFFSET);

	amb_gpio_raw_unlock(&flags);

	return 0;
}

static int amb_gpio_irq_set_wake(struct irq_data *data, unsigned int on)
{
#if defined(CONFIG_PM)
	struct amb_gpio_chip *amb_gpio = dev_get_drvdata(amb_gc.parent);
	u32 bank = PINID_TO_BANK(data->hwirq);
	u32 offset = PINID_TO_OFFSET(data->hwirq);

	if (on)
		amb_gpio->bank[bank].irq_wake_mask |= (1 << offset);
	else
		amb_gpio->bank[bank].irq_wake_mask &= ~(1 << offset);
#endif
	return 0;
}

static struct irq_chip amb_gpio_irqchip = {
	.name		= "GPIO",
	.irq_enable	= amb_gpio_irq_enable,
	.irq_disable	= amb_gpio_irq_disable,
	.irq_ack	= amb_gpio_irq_ack,
	.irq_mask	= amb_gpio_irq_mask,
	.irq_mask_ack	= amb_gpio_irq_mask_ack,
	.irq_unmask	= amb_gpio_irq_unmask,
	.irq_set_type	= amb_gpio_irq_set_type,
	.irq_set_wake	= amb_gpio_irq_set_wake,
	.flags		= IRQCHIP_SET_TYPE_MASKED | IRQCHIP_MASK_ON_SUSPEND,
};

static int amb_gpio_irqdomain_map(struct irq_domain *d,
			unsigned int irq, irq_hw_number_t hwirq)
{
	struct amb_gpio_chip *amb_gpio;

	amb_gpio = (struct amb_gpio_chip *)d->host_data;

	irq_set_chip_and_handler(irq, &amb_gpio_irqchip, handle_level_irq);
	irq_set_chip_data(irq, amb_gpio->bank[PINID_TO_BANK(hwirq)].regbase);
	irq_set_noprobe(irq);

	return 0;
}

const struct irq_domain_ops amb_gpio_irq_domain_ops = {
	.map = amb_gpio_irqdomain_map,
	.xlate = irq_domain_xlate_twocell,
};


static void amb_gpio_handle_irq(struct irq_desc *desc)
{
	struct irq_chip *irqchip;
	struct amb_gpio_chip *amb_gpio;
	u32 i, irq, gpio_mis, gpio_hwirq, gpio_irq;

	irqchip = irq_desc_get_chip(desc);
	chained_irq_enter(irqchip, desc);

	amb_gpio = irq_desc_get_handler_data(desc);
	irq = irq_desc_get_irq(desc);

	/* find the GPIO bank generating this irq */
	for (i = 0; i < amb_gpio->bank_num; i++) {
		if (amb_gpio->bank[i].irq == irq)
			break;
	}

	if (i == amb_gpio->bank_num)
		return;

	gpio_mis = readl_relaxed(amb_gpio->bank[i].regbase + GPIO_MIS_OFFSET);
	if (gpio_mis) {
		gpio_hwirq = i * GPIO_BANK_SIZE + ffs(gpio_mis) - 1;
		gpio_irq = irq_find_mapping(amb_gpio->domain, gpio_hwirq);
		generic_handle_irq(gpio_irq);
	}

	chained_irq_exit(irqchip, desc);
}

static int amb_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *parent;
	struct amb_gpio_chip *amb_gpio;
	struct amb_gpio_bank *bank;
	int i, rval;

	amb_gpio = devm_kzalloc(&pdev->dev, sizeof(*amb_gpio), GFP_KERNEL);
	if (!amb_gpio) {
		dev_err(&pdev->dev, "failed to allocate memory for amb_gpio\n");
		return -ENOMEM;
	}

	amb_gpio->bank_num = of_irq_count(np);
	if (amb_gpio->bank_num == 0) {
		dev_err(&pdev->dev, "Invalid gpio bank num(irq)\n");
		return -EINVAL;
	}

	parent = of_get_parent(np);

	for (i = 0; i < amb_gpio->bank_num; i++) {
		bank = &amb_gpio->bank[i];
		bank->regbase = of_iomap(parent, i);
		if (bank->regbase == NULL) {
			dev_err(&pdev->dev, "devm_ioremap() failed\n");
			return -ENOMEM;
		}

		bank->irq = irq_of_parse_and_map(np, i);
		if (bank->irq == 0) {
			dev_err(&pdev->dev, "no irq for gpio[%d]!\n", i);
			return -ENXIO;
		}

		writel_relaxed(0xffffffff, bank->regbase + GPIO_ENABLE_OFFSET);
	}

	/* iomux_base will get NULL if not existed */
	amb_gpio->iomux_base = of_iomap(parent, i);

	of_node_put(parent);

	amb_gpio->gc = &amb_gc;
	amb_gpio->gc->parent = &pdev->dev;
	amb_gpio->gc->ngpio = amb_gpio->bank_num * 32;
	rval = gpiochip_add(amb_gpio->gc);
	if (rval) {
		dev_err(&pdev->dev,
			"failed to register gpio_chip %s\n", amb_gpio->gc->label);
		return rval;
	}

	/* Initialize GPIO irq */
	amb_gpio->domain = irq_domain_add_linear(np, amb_gpio->gc->ngpio,
					&amb_gpio_irq_domain_ops, amb_gpio);
	if (!amb_gpio->domain) {
		pr_err("%s: Failed to create irqdomain\n", np->full_name);
		return -ENOSYS;
	}

	if (of_property_read_u32(np, "amb,banks_irq_mask", &amb_gpio->banks_irq_mask) < 0) {
		amb_gpio->banks_irq_mask = 0;
	}

	for (i = 0; i < amb_gpio->bank_num; i++) {
		bank = &amb_gpio->bank[i];
		if ((1 << i) & amb_gpio->banks_irq_mask) {
			const char *affinity;
			int ret;

			dev_info(&pdev->dev, "Enabling GPIO Group %d interrupts.\n", i);
			irq_set_irq_type(bank->irq, IRQ_TYPE_LEVEL_HIGH);
			irq_set_handler_data(bank->irq, amb_gpio);
			irq_set_chained_handler(bank->irq, amb_gpio_handle_irq);
			ret = of_property_read_string_index(np, "amb,banks_irq_affinity", i, &affinity);
			if (ret == 0) {
				ret = __bitmap_parse(affinity, strlen(affinity), 0, cpumask_bits(&bank->affinity), nr_cpu_ids);
				if (ret == 0)
					irq_set_affinity(bank->irq, &bank->affinity);
				else
					printk("amb,banks_irq_affinity[%d]=%s in invalid err=%d\n",
							i, affinity, ret);
			}
		}
	}
#if defined(CONFIG_PM) && defined(CONFIG_ARCH_AMBARELLA_AMBALINK)
	memset(ambalink_gpio_linux_only_mask, 0x0, sizeof(u32) * GPIO_MAX_BANK_NUM);
#endif

	platform_set_drvdata(pdev, amb_gpio);

	dev_info(&pdev->dev, "Ambarella GPIO driver registered\n");

	return 0;
}

#ifdef CONFIG_PM
static int amb_gpio_irq_suspend(void)
{
	struct amb_gpio_chip *amb_gpio;
	struct amb_gpio_bank *bank;
	int i;
	unsigned long flags;

	amb_gpio = dev_get_drvdata(amb_gc.parent);
	if (amb_gpio == NULL) {
		pr_err("No device for ambarella gpio irq\n");
		return -ENODEV;
	}

	for (i = 0; i < amb_gpio->bank_num; i++) {
		bank = &amb_gpio->bank[i];

		amb_gpio_raw_lock(&flags);

		bank->afsel = readl_relaxed(bank->regbase + GPIO_AFSEL_OFFSET);
		bank->dir = readl_relaxed(bank->regbase + GPIO_DIR_OFFSET);
		bank->is = readl_relaxed(bank->regbase + GPIO_IS_OFFSET);
		bank->ibe = readl_relaxed(bank->regbase + GPIO_IBE_OFFSET);
		bank->iev = readl_relaxed(bank->regbase + GPIO_IEV_OFFSET);
		bank->ie = readl_relaxed(bank->regbase + GPIO_IE_OFFSET);
		bank->mask = ~bank->afsel;
#ifndef CONFIG_ARCH_AMBARELLA_AMBALINK
		/* do not overwrite the gpio mask register */
		writel_relaxed(bank->mask, bank->regbase + GPIO_MASK_OFFSET);
#endif
		bank->data = readl_relaxed(bank->regbase + GPIO_DATA_OFFSET);

		if (bank->irq_wake_mask) {
			writel_relaxed(bank->irq_wake_mask, bank->regbase + GPIO_IE_OFFSET);
			pr_info("gpio_irq[%p]: irq_wake[0x%08X]\n",
						bank->regbase, bank->irq_wake_mask);
		}

		amb_gpio_raw_unlock(&flags);
	}

	return 0;
}

#ifdef CONFIG_ARCH_AMBARELLA_AMBALINK
void ambarella_gpio_create_linux_only_mask(u32 gpio)
{
	ambalink_gpio_linux_only_mask[gpio >> 5] |= 1 << (gpio % 32);
}
EXPORT_SYMBOL(ambarella_gpio_create_linux_only_mask);

static void amb_gpio_irq_resume(void)
{
	struct amb_gpio_chip *amb_gpio;
	struct amb_gpio_bank *bank;
	int i;
	u32 val;
	unsigned long flags;

	amb_gpio = dev_get_drvdata(amb_gc.parent);
	if (amb_gpio == NULL) {
		pr_err("No device for ambarella gpio irq\n");
		return;
	}

	for (i = 0; i < amb_gpio->bank_num; i++) {
		bank = &amb_gpio->bank[i];

		amb_gpio_raw_lock(&flags);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_AFSEL_OFFSET),
				bank->afsel,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_AFSEL_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_DIR_OFFSET),
				bank->dir,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_DIR_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_MASK_OFFSET),
				bank->mask,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_MASK_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_DATA_OFFSET),
				bank->data,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_DATA_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_IS_OFFSET),
				bank->is,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_IS_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_IBE_OFFSET),
				bank->ibe,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_IBE_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_IEV_OFFSET),
				bank->iev,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_IEV_OFFSET);

		val = linux_only_mask_gpio(
				readl_relaxed(bank->regbase + GPIO_IE_OFFSET),
				bank->ie,
				ambalink_gpio_linux_only_mask[i]);
		writel_relaxed(val, bank->regbase + GPIO_IE_OFFSET);

		/* Write 1 to enable all GPIO. */
		writel_relaxed(0x1, bank->regbase + GPIO_ENABLE_OFFSET);

		amb_gpio_raw_unlock(&flags);
	}
}
#else
static void amb_gpio_irq_resume(void)
{
	struct amb_gpio_chip *amb_gpio;
	struct amb_gpio_bank *bank;
	int i;

	amb_gpio = dev_get_drvdata(amb_gc.parent);
	if (amb_gpio == NULL) {
		pr_err("No device for ambarella gpio irq\n");
		return;
	}

	for (i = 0; i < amb_gpio->bank_num; i++) {
		bank = &amb_gpio->bank[i];
		writel_relaxed(bank->afsel, bank->regbase + GPIO_AFSEL_OFFSET);
		writel_relaxed(bank->dir, bank->regbase + GPIO_DIR_OFFSET);
		writel_relaxed(bank->mask, bank->regbase + GPIO_MASK_OFFSET);
		writel_relaxed(bank->data, bank->regbase + GPIO_DATA_OFFSET);
		writel_relaxed(bank->is, bank->regbase + GPIO_IS_OFFSET);
		writel_relaxed(bank->ibe, bank->regbase + GPIO_IBE_OFFSET);
		writel_relaxed(bank->iev, bank->regbase + GPIO_IEV_OFFSET);
		writel_relaxed(bank->ie, bank->regbase + GPIO_IE_OFFSET);
		writel_relaxed(0xffffffff, bank->regbase + GPIO_ENABLE_OFFSET);
	}
}
#endif	/* CONFIG_ARCH_AMBARELLA_AMBALINK */

struct syscore_ops amb_gpio_irq_syscore_ops = {
	.suspend	= amb_gpio_irq_suspend,
	.resume		= amb_gpio_irq_resume,
};

#endif

static const struct of_device_id amb_gpio_dt_match[] = {
	{ .compatible = "ambarella,gpio" },
	{},
};
MODULE_DEVICE_TABLE(of, amb_gpio_dt_match);

static struct platform_driver amb_gpio_driver = {
	.probe	= amb_gpio_probe,
	.driver	= {
		.name	= "ambarella-gpio",
		.of_match_table = of_match_ptr(amb_gpio_dt_match),
	},
};

static int __init amb_gpio_drv_register(void)
{
#ifdef CONFIG_PM
	register_syscore_ops(&amb_gpio_irq_syscore_ops);
#endif

	return platform_driver_register(&amb_gpio_driver);
}
postcore_initcall(amb_gpio_drv_register);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("Ambarella SoC GPIO driver");
MODULE_LICENSE("GPL v2");

