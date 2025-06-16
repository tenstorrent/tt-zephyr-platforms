/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bh_arc_priv.h"

#include <tenstorrent/bh_chip.h>

#include <zephyr/drivers/gpio.h>

struct tt_smbus_stm32_config {
	const struct pinctrl_dev_config *pcfg;
	const struct device *i2c_dev;
};

void bh_chip_set_straps(struct bh_chip *chip)
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
			gpio_pin_configure_dt(strap_ptr, GPIO_OUTPUT_ACTIVE);
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
