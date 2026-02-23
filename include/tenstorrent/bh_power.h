/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BH_POWER_H
#define BH_POWER_H

#include <stdbool.h>
#include <stdint.h>

int32_t bh_set_l2cpu_enable(bool enable);
bool bh_get_aiclk_busy(void);
bool bh_get_mrisc_power_state(void);

#endif
