/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_JTAG_BOOTROM_PINS_H_
#define TENSTORRENT_JTAG_BOOTROM_PINS_H_

#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct gpio_dt_spec TCK;
extern const struct gpio_dt_spec TDI;
extern const struct gpio_dt_spec TDO;
extern const struct gpio_dt_spec TMS;
extern const struct gpio_dt_spec TRST;

#ifdef __cplusplus
}
#endif

#endif
