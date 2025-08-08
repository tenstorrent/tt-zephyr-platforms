/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SPI_FLASH_BUF_H_
#define _SPI_FLASH_BUF_H_

#include <stdint.h>
#include <stddef.h>

#include <zephyr/device.h>

#define TEMP_SPI_BUFFER_SIZE 4096

int spi_arc_dma_transfer_to_tile(const struct device *dev, const char *tag, uint8_t *buf,
				 size_t buf_size, uint8_t *tlb_dst);

#endif
