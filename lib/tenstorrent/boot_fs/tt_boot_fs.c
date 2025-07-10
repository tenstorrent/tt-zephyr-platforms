/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>

#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/devicetree.h>

uint32_t tt_boot_fs_next(uint32_t last_fd_addr)
{
	return (last_fd_addr + sizeof(tt_boot_fs_fd));
}

int tt_bootfs_ng_read(const struct device *dev, uint32_t addr, uint8_t *buffer, size_t size)
{
	if (!device_is_ready(dev)) {
		return TT_BOOT_FS_ERR;
	}

	return flash_read(dev, addr, buffer, size);
}

int tt_bootfs_ng_write(const struct device *dev, uint32_t addr, const uint8_t *buffer, size_t size)
{
	if (!device_is_ready(dev)) {
		return TT_BOOT_FS_ERR;
	}

	if (!buffer) {
		return TT_BOOT_FS_ERR;
	}

	return flash_write(dev, addr, buffer, size);
}

int tt_bootfs_ng_erase(const struct device *dev, uint32_t addr, size_t size)
{
	if (!device_is_ready(dev)) {
		return TT_BOOT_FS_ERR;
	}

	if (size == 0) {
		return TT_BOOT_FS_ERR;
	}

	return flash_erase(dev, addr, size);
}

uint32_t tt_boot_fs_cksum(uint32_t cksum, const uint8_t *data, size_t num_bytes)
{
	if (num_bytes == 0 || data == NULL) {
		return 0;
	}

	/* Always read 1 fewer word, and handle the 4 possible alignment cases outside the loop */
	const uint32_t num_dwords = num_bytes / sizeof(uint32_t) - 1;
	uint32_t *data_as_dwords = (uint32_t *)data;

	for (uint32_t i = 0; i < num_dwords; i++) {
		cksum += *data_as_dwords++;
	}

	switch (num_bytes % 4) {
	case 0:
		cksum += *data_as_dwords & 0xffffffff;
		break;
	default:
		__ASSERT(false, "size %zu is not a multiple of 4", num_bytes);
		break;
	}

	return cksum;
}

static tt_checksum_res_t calculate_and_compare_checksum(uint8_t *data, size_t num_bytes,
							uint32_t expected, bool skip_checksum)
{
	uint32_t calculated_checksum;

	if (!skip_checksum) {
		calculated_checksum = tt_boot_fs_cksum(0, data, num_bytes);
		if (calculated_checksum != expected) {
			return TT_BOOT_FS_CHK_FAIL;
		}
	}

	return TT_BOOT_FS_CHK_OK;
}

int tt_boot_fs_ls(const struct device *dev, tt_boot_fs_fd *fds, size_t nfds)
{
	if (!device_is_ready(dev)) {
		return TT_BOOT_FS_ERR;
	}

	if (!fds || nfds == 0) {
		return TT_BOOT_FS_ERR;
	}

	tt_boot_fs_fd temp_fds[CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX];

	if (tt_bootfs_ng_read(dev, TT_BOOT_FS_FD_HEAD_ADDR, (uint8_t *)temp_fds,
			      sizeof(temp_fds)) != 0) {
		return TT_BOOT_FS_ERR;
	}

	size_t count = 0;
	/* Iterate through the fds now in RAM */
	for (size_t i = 0; i < CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX; i++) {

		if (count >= nfds) {
			break;
		}

		tt_boot_fs_fd *current_fd = &temp_fds[i];

		if (current_fd->flags.f.invalid) {
			break;
		}

		tt_checksum_res_t chk_res = calculate_and_compare_checksum(
			(uint8_t *)current_fd, sizeof(tt_boot_fs_fd) - sizeof(uint32_t),
			current_fd->fd_crc, false);

		if (chk_res == TT_BOOT_FS_CHK_OK) {
			fds[count] = *current_fd;
			count++;
		}
	}
	return count;
}

const tt_boot_fs_fd *find_fd_by_tag(const uint8_t *tag, tt_boot_fs_fd *fds, int count)
{
	for (int i = 0; i < count; i++) {
		if (strncmp(fds[i].image_tag, tag, sizeof(fds[i].image_tag)) == 0) {
			return &fds[i]; /* return pointer to fd in array */
		}
	}

	/* File descriptor not found */
	return NULL;
}
