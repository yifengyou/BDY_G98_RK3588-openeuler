// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) HiSilicon Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <linux/acpi_iort.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/platform_device.h>
#include <ub/ubfi/ubfi.h>

int ub_update_msi_domain(struct device *dev,
			 enum irq_domain_bus_token bus_token)
{
#ifdef CONFIG_GENERIC_MSI_IRQ
	struct fwnode_handle *fwnode;
	struct irq_domain *domain;

	domain = dev->msi.domain;
	if (!domain) {
		dev_err(dev, "find base irq domain failed!\n");
		return -ENODEV;
	}

	fwnode = domain->fwnode;
	if (!fwnode) {
		dev_err(dev, "find fwnode failed!\n");
		return -ENODEV;
	}

	domain = irq_find_matching_fwnode(fwnode, bus_token);
	if (!domain) {
		dev_err(dev, "find irq domain failed!\n");
		return -ENODEV;
	}

	/* Update msi domain with new bus_token */
	dev_set_msi_domain(dev, domain);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(ub_update_msi_domain);

int ubrt_register_gsi(u32 hwirq, int trigger, int polarity, const char *name,
		      struct resource *res)
{
#ifdef CONFIG_ACPI
	int irq;

	if (!res) {
		pr_err("ub register gsi, res is null\n");
		return -EINVAL;
	}

	irq = acpi_register_gsi(NULL, hwirq, trigger, polarity);
	if (irq <= 0) {
		pr_err("could not register ub gsi hwirq %u\n", hwirq);
		return -EINVAL;
	}

	res->name = name;
	res->start = irq;
	res->end = irq;
	res->flags = IORESOURCE_IRQ;
	return 0;
#else
	return -EINVAL;
#endif
}
EXPORT_SYMBOL_GPL(ubrt_register_gsi);

void ubrt_unregister_gsi(u32 hwirq)
{
#ifdef CONFIG_ACPI
	acpi_unregister_gsi(hwirq);
#endif
}
EXPORT_SYMBOL_GPL(ubrt_unregister_gsi);

