/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/tt_boot_fs.h>

#include <zephyr/ztest.h>

#define MRISC_FW_CFG_TAG   "memfwcfg"
#define STATIC_BUFFER_SIZE 0x9000

static uint8_t large_sram_buffer[STATIC_BUFFER_SIZE] __aligned(4);

/* Confirm that reading value through temp buffer still provides expected result */
ZTEST(spi_flash_buf, test_read_to_buf)
{
	int rc;
	size_t fw_size = 0;

	rc = tt_boot_fs_get_file(&boot_fs_data, MRISC_FW_CFG_TAG, large_sram_buffer,
				 STATIC_BUFFER_SIZE, &fw_size);
	zassert_equal(rc, TT_BOOT_FS_OK, "%s(%s) failed: %d", "tt_boot_fs_get_file",
		      MRISC_FW_CFG_TAG, rc);

	tt_boot_fs_fd fd_data;
	uint8_t *buf = NULL;

	tt_boot_fs_find_fd_by_tag(&boot_fs_data, MRISC_FW_CFG_TAG, &fd_data);
	rc = spi_flash_read_data_to_buf(fd_data.spi_addr, fd_data.flags.f.image_size, &buf);
	zassert_equal(rc, 0, "%s(%s) failed: %d", "spi_flash_read_data_to_buf", MRISC_FW_CFG_TAG,
		      rc);

	zassert_mem_equal(buf, large_sram_buffer, fd_data.flags.f.image_size, "Buffers differ");

	k_free(buf);
	buf = NULL;
}

ZTEST_SUITE(spi_flash_buf, NULL, NULL, NULL, NULL, NULL);
