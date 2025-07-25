/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SPI_FLASH_BUF_H_
#define _SPI_FLASH_BUF_H_

#include <stdint.h>
#include <stddef.h>

#define TEMP_SPI_BUFFER_SIZE 4096

int spi_flash_read_data_to_buf(uint32_t spi_address, size_t image_size, uint8_t **buf);
int spi_arc_dma_transfer_to_tile(uint32_t spi_address, size_t image_size, uint8_t *tlb_dst);

#endif
