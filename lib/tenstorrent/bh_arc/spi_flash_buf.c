/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_arc_hs.h>

LOG_MODULE_REGISTER(spi_flash_buf, CONFIG_TT_APP_LOG_LEVEL);

static const struct device *const arc_dma_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dma0));

int spi_transfer_by_parts(const struct device *dev, size_t spi_address, size_t image_size,
			  uint8_t *buf, size_t buf_size, uint8_t *tlb_dst,
			  int (*cb)(uint8_t *src, uint8_t *dst, size_t len))
{
	if ((buf == NULL) || (buf_size == 0)) {
		return -EINVAL;
	}

	if (image_size > (size_t)INT32_MAX) {
		return -E2BIG;
	}

	int rc = 0;

	for (size_t offset = 0, len = MIN(buf_size, image_size); len > 0;
	     image_size -= len, offset += len, len = MIN(buf_size, image_size)) {

		rc = flash_read(dev, spi_address + offset, buf, len);
		if (rc < 0) {
			LOG_ERR("%s() failed: %d", "flash_read", rc);
			break;
		}

		rc = cb(buf, tlb_dst + offset, len);
		if (rc < 0) {
			break;
		}
	}
	return rc;
}

static int arc_dma_transfer_wrapper(uint8_t *src, uint8_t *dst, size_t len)
{
	if (dma_arc_hs_transfer(arc_dma_dev, 0, src, dst, len) < 0) {
		LOG_ERR("%s() failed: %d", "dma_arc_hs_transfer", -EIO);
		return -EIO;
	}
	return 0;
}

int spi_arc_dma_transfer_to_tile(const struct device *dev, size_t spi_address, size_t image_size,
				 uint8_t *buf, size_t buf_size, uint8_t *tlb_dst)
{
	return spi_transfer_by_parts(dev, spi_address, image_size, buf, buf_size, tlb_dst,
				     arc_dma_transfer_wrapper);
}
