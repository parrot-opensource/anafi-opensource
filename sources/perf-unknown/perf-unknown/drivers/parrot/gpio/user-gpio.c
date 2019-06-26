#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>

static int user_gpio_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cnp;

	for_each_child_of_node(np, cnp) {
		const char *name = NULL;
		unsigned int gpio;
		unsigned int debounce;
		unsigned int val;
		unsigned int irq_edge_rising = 0;
		unsigned int irq_edge_falling = 0;

		enum of_gpio_flags flags;

		name = cnp->name;
		gpio = of_get_gpio_flags(cnp, 0, &flags);

		if (!gpio_is_valid(gpio)) {
			continue;
		}

		if (of_find_property(cnp, "active-low", NULL)) {
			flags |= GPIOF_ACTIVE_LOW;
		}

		if (devm_gpio_request_one(&pdev->dev, gpio, flags, name)) {
			continue;
		}

		if (!of_property_read_u32(cnp, "debounce", &debounce)) {
			gpio_set_debounce(gpio, debounce);
		}

		if (!of_property_read_u32(cnp, "output", &val)) {
			gpio_direction_output(gpio, val);
		}
		else {
			gpio_direction_input(gpio);
		}

		if (gpio_export(gpio, true)) {
			continue;
		}

		if (of_find_property(cnp, "interrupt-edge-both", NULL) ||
				of_find_property(cnp, "interrupt-edge-rising", NULL)) {
			irq_edge_rising = 1;
		}

		if (of_find_property(cnp, "interrupt-edge-both", NULL) ||
				of_find_property(cnp, "interrupt-edge-falling", NULL)) {
			irq_edge_falling = 1;
		}

		gpio_sysfs_set_edge(gpio, irq_edge_falling, irq_edge_rising);

		if (gpio_export_link(&pdev->dev, name, gpio)) {
			gpio_unexport(gpio);
			continue;
		}

		dev_info(&pdev->dev, "gpio %s (%d) exported\n", name, gpio);
	}

	return 0;
}

static int user_gpio_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cnp;

	for_each_child_of_node(np, cnp) {
		const char *name = NULL;
		int gpio = -1;

		name = cnp->name;
		gpio = of_get_gpio(cnp, 0);

		if (!gpio_is_valid(gpio)) {
			continue;
		}

		sysfs_remove_link(&pdev->dev.kobj, name);
		gpio_unexport(gpio);
	}

	return 0;
}

static struct of_device_id user_gpio_of_match[] = {
	{ .compatible = "parrot,user-gpio" },
	{}
};

static struct platform_driver user_gpio_driver = {
	.probe = user_gpio_probe,
	.remove = user_gpio_remove,
	.driver = {
		.name           = "user_gpio",
		.owner          = THIS_MODULE,
		.of_match_table = of_match_ptr(user_gpio_of_match),
	},
};

module_platform_driver(user_gpio_driver);

MODULE_AUTHOR("Ronan Chauvin <ronan.chauvin@parrot.com>");
MODULE_DESCRIPTION("");
MODULE_LICENSE("GPL v2");
