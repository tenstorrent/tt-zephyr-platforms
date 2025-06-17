/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PLL_H_
#define ZEPHYR_INCLUDE_DRIVERS_PLL_H_

#include <zephyr/device.h>
#include <stdint.h>

void pll_bypass_all(
    const struct device *dev
);

uint32_t pll_GetAICLK(
    const struct device *dev
);

uint32_t pll_GetARCCLK(
    const struct device *dev
);

uint32_t pll_GetAXICLK(
    const struct device *dev
);

uint32_t pll_GetAPBCLK(
    const struct device *dev
);

uint32_t pll_GetL2CPUCLK(
    const struct device *dev,
    uint8_t l2cpu_num
);

#endif /* ZEPHYR_INCLUDE_DRIVERS_PLL_H_ */
