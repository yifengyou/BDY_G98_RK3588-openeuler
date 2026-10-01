/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ACPI_BUTTON_H
#define ACPI_BUTTON_H

#include <linux/notifier.h>

#define ACPI_BUTTON_HID_POWER	"PNP0C0C"
#define ACPI_BUTTON_HID_LID	"PNP0C0D"
#define ACPI_BUTTON_HID_SLEEP	"PNP0C0E"

#if IS_ENABLED(CONFIG_ACPI_BUTTON)
extern int acpi_lid_open(void);
#else
static inline int acpi_lid_open(void)
{
	return 1;
}
#endif /* IS_ENABLED(CONFIG_ACPI_BUTTON) */

#if IS_ENABLED(CONFIG_ACPI_POWER_NOTIFIER_CHAIN)
int register_acpi_power_notifier(struct notifier_block *nb);
int unregister_acpi_power_notifier(struct notifier_block *nb);
#else
static inline int register_acpi_power_notifier(struct notifier_block *nb)
{
	return -EINVAL;
};
static inline int unregister_acpi_power_notifier(struct notifier_block *nb)
{
	return -EINVAL;
};
#endif /* IS_ENABLED(CONFIG_ACPI_POWER_NOTIFIER_CHAIN) */

#endif /* ACPI_BUTTON_H */
