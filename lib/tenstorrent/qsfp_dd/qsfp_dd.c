/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/qsfp_dd.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

static const struct device *const i2c3 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c3));
static const struct gpio_dt_spec mcu_conn_i2c_en =
	GPIO_DT_SPEC_GET_OR(DT_PATH(mcu_conn_i2c_en), gpios, {0});

uint8_t gpio_xp_addrs[] = {0x38, 0x39, 0x3a, 0x3b};

int gpio_xp_set_config(uint8_t addr, uint8_t val)
{
	uint8_t cmd_data[2] = {GPIO_XP_REG_CONFIG, val};

	return i2c_write(i2c3, cmd_data, 2, addr);
}

int gpio_xp_set_output(uint8_t addr, uint8_t val)
{
	uint8_t cmd_data[2] = {GPIO_XP_REG_OUTPUT_PORT, val};

	return i2c_write(i2c3, cmd_data, 2, addr);
}

int enable_active_qsfp_dd(void)
{
	int ret;

	/* Enable communication with GPIO expander */
	ret = gpio_pin_configure_dt(&mcu_conn_i2c_en, GPIO_OUTPUT_ACTIVE);
	if (ret != 0) {
		return ret;
	}

	for (int i = 0; i < ARRAY_SIZE(gpio_xp_addrs); i++) {
		/* Configure RST (P1), MODSEL (P2), LPMODE (P3) as output */
		ret = gpio_xp_set_config(gpio_xp_addrs[i], 0xf1);
		if (ret != 0) {
			return ret;
		}
		/* Set RST (P1) output to high */
		ret = gpio_xp_set_output(gpio_xp_addrs[i], 0x2);
		if (ret != 0) {
			return ret;
		}
	}

	/* Disable communication with GPIO expander */
	ret = gpio_pin_configure_dt(&mcu_conn_i2c_en, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		return ret;
	}

	return ret;
}
