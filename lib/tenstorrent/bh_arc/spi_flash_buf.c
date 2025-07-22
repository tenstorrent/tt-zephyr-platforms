/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arc_dma.h"

#include <stdlib.h>

#include <tenstorrent/spi_flash_buf.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_flash_buf, CONFIG_TT_APP_LOG_LEVEL);

static const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));

int spi_flash_read_data_to_buf(uint32_t spi_address, size_t image_size, uint8_t **buf)
{
	*buf = (uint8_t *)k_aligned_alloc(4, image_size);
	/* Return allocation failures */
	if (*buf == NULL) {

		return -EIO;
	}

	int error = 0;

	error = flash_read(flash, spi_address, *buf, image_size);
	if (error < 0) {
		k_free(*buf);
		*buf = NULL;
	}

	return error;
}

int spi_arc_dma_transfer_to_tile(uint32_t spi_address, size_t image_size, uint8_t *tlb_dst)
{
	uint8_t *buf = NULL;

	for (size_t offset = 0; offset < image_size; offset += TEMP_SPI_BUFFER_SIZE) {
		size_t len = MIN(TEMP_SPI_BUFFER_SIZE, image_size - offset);

		if (spi_flash_read_data_to_buf(spi_address + offset, len, &buf)) {
			LOG_ERR("%s failed: %d", "spi_flash_read_data_to_buf", -EIO);
			return -EIO;
		}

		bool dma_pass = ArcDmaTransfer(buf, tlb_dst + offset, len);

		k_free(buf);
		buf = NULL;

		if (!dma_pass) {
			return -1;
		}
	}
	return 0;
}
