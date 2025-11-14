/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARC_DMA_H_
#define ARC_DMA_H_

#include <stdbool.h>
#include <stdint.h>

 /* Declaration for ArcDmaTransfer used by libpciesd.a */
bool ArcDmaTransfer(const void *src, void *dst, uint32_t len);

#endif /* ARC_DMA_H_ */
