/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SPI_FLASH_BUF_H_
#define _SPI_FLASH_BUF_H_

#include "init.h"

#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>

int spi_transfer_by_parts(const struct device *dev, size_t spi_address, size_t image_size,
			  uint8_t *buf, size_t buf_size, uint8_t *tlb_dst,
			  int (*cb)(uint8_t *src, uint8_t *dst, size_t len));
int spi_arc_dma_transfer_to_tile(const struct device *dev, size_t spi_address, size_t image_size,
				 uint8_t *buf, size_t buf_size, uint8_t *tlb_dst);

#endif
