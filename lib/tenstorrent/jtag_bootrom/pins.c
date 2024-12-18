/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

const struct gpio_dt_spec TCK = GPIO_DT_SPEC_GET(DT_INST(0, zephyr_jtag_gpio), tck_gpios);
const struct gpio_dt_spec TDI = GPIO_DT_SPEC_GET(DT_INST(0, zephyr_jtag_gpio), tdi_gpios);
const struct gpio_dt_spec TDO = GPIO_DT_SPEC_GET(DT_INST(0, zephyr_jtag_gpio), tdo_gpios);
const struct gpio_dt_spec TMS = GPIO_DT_SPEC_GET(DT_INST(0, zephyr_jtag_gpio), tms_gpios);
const struct gpio_dt_spec TRST = GPIO_DT_SPEC_GET_OR(DT_INST(0, zephyr_jtag_gpio), trst_gpios, {0});
