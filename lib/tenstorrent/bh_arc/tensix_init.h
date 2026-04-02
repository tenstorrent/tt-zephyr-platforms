/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

void TensixInit(void);
void EnableTensixCG(bool broadcast, uint8_t noc_x, uint8_t noc_y);
