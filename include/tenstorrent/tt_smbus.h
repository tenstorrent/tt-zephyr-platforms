/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DRIVERS_TT_SMBUS_STM32_H_
#define DRIVERS_TT_SMBUS_STM32_H_

#include <zephyr/device.h>

void tt_smbus_stm32_set_abort_ptr(const struct device *dev, unsigned int *abort);

#endif
