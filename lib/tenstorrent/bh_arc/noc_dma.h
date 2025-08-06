/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NOC_DMA_H_INCLUDED
#define NOC_DMA_H_INCLUDED

#include <stdint.h>

bool noc_dma_read(uint8_t local_x, uint8_t local_y, uint64_t local_addr, uint8_t remote_x,
		  uint8_t remote_y, uint64_t remote_addr, uint32_t size, bool wait_for_done);
bool noc_dma_write(uint8_t local_x, uint8_t local_y, uint64_t local_addr, uint8_t remote_x,
		   uint8_t remote_y, uint64_t remote_addr, uint32_t size, bool wait_for_done);
bool noc_dma_broadcast(uint8_t local_x, uint8_t local_y, uint64_t addr, uint32_t size);
#endif
