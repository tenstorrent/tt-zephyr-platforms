/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arc_dma.h"

#include <stdlib.h>

#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spi_flash_buf, CONFIG_TT_APP_LOG_LEVEL);

int spi_arc_dma_transfer_to_tile(const struct device *dev, const char *tag, uint8_t *buf,
				 size_t buf_size, uint8_t *tlb_dst)
{
	int rc;
	tt_boot_fs_fd tag_fd;
	size_t image_size;
	size_t spi_address;

	if ((buf == NULL) || (buf_size == 0)) {
		return -EINVAL;
	}

	rc = tt_boot_fs_find_fd_by_tag(dev, tag, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "tt_boot_fs_ls", rc);
		return rc;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	for (size_t offset = 0, len = MIN(buf_size, image_size); len > 0;
	     image_size -= len, offset += len, len = MIN(buf_size, image_size)) {

		rc = flash_read(dev, spi_address + offset, buf, len);
		if (rc < 0) {
			LOG_ERR("%s() failed: %d", "flash_read", rc);
			return rc;
		}

		if (!ArcDmaTransfer(buf, tlb_dst + offset, len)) {
			LOG_ERR("%s() failed: %d", "ArcDmaTransfer", -EIO);
			return -EIO;
		}
	}
	return 0;
}
