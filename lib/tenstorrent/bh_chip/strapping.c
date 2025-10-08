/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bh_arc_priv.h"

#include <tenstorrent/bh_chip.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

void bh_chip_set_straps(struct bh_chip *chip)
{
	int ret;
	bharc_enable_i2cbus(&chip->config.arc);
	const struct gpio_dt_spec straps[] = {
		chip->config.strapping.gpio6,
		chip->config.strapping.gpio38,
		chip->config.strapping.gpio39,
		chip->config.strapping.gpio40,
	};

	ARRAY_FOR_EACH_PTR(straps, strap_ptr) {
		if (strap_ptr->port != NULL) {
			ret = gpio_pin_configure_dt(strap_ptr, GPIO_OUTPUT_ACTIVE);
			if (ret < 0) {
				printk("Failed to configure strap %s: %d", strap_ptr->port->name,
				       ret);
				i2c_recover_bus(chip->config.arc.smbus.bus);
				ret = gpio_pin_configure_dt(strap_ptr, GPIO_OUTPUT_ACTIVE);
				if (ret < 0) {
					printk("Failed to configure strap after i2c recover %s: "
					       "%d\n",
					       strap_ptr->port->name, ret);
				} else {
					printk("Strap %s successfully configured after i2c "
					       "recover\n",
					       strap_ptr->port->name);
				}
			}
		}
	}
	bharc_disable_i2cbus(&chip->config.arc);
}

void bh_chip_unset_straps(struct bh_chip *chip)
{
	bharc_enable_i2cbus(&chip->config.arc);
	const struct gpio_dt_spec straps[] = {
		chip->config.strapping.gpio6,
		chip->config.strapping.gpio38,
		chip->config.strapping.gpio39,
		chip->config.strapping.gpio40,
	};

	ARRAY_FOR_EACH_PTR(straps, strap_ptr) {
		if (strap_ptr->port != NULL) {
			gpio_pin_configure_dt(strap_ptr, GPIO_INPUT);
		}
	}
	bharc_disable_i2cbus(&chip->config.arc);
}
