/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/drivers/gpio.h>

extern const struct gpio_dt_spec TCK;
extern const struct gpio_dt_spec TDI;
extern const struct gpio_dt_spec TDO;
extern const struct gpio_dt_spec TMS;
extern const struct gpio_dt_spec TRST;
