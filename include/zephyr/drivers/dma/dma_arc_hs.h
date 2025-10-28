/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <zephyr/drivers/dma.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Blocking memory-to-memory transfer using ARC HS DMA
 *
 * @param dev     DMA device (from DEVICE_DT_GET)
 * @param channel DMA channel (0 to N-1)
 * @param src     Source address (4-byte aligned)
 * @param dst     Destination address (4-byte aligned)
 * @param len     Transfer length in bytes
 * @return 0 on success, negative errno on error
 */

int dma_arc_hs_transfer(const struct device *dev, uint32_t channel, const void *src, void *dst,
                        size_t len);

#ifdef __cplusplus
}
#endif
