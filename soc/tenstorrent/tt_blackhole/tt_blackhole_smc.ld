/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* CSM @ 0x10000000 is physically 512kB but we limit the amount
 * available to the linker to leave space for special purposes.
 *
 * Note that linker scripts do not allow expressions in this context.
 */
#define SRAM_START 0x10000000
#define SRAM_SIZE 0x30000

#include <zephyr/arch/arc/v2/linker.ld>
