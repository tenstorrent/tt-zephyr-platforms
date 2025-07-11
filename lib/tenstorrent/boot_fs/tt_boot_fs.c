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

static int tt_bootfs_ng_initial_check(const tt_boot_fs_ng *fs)
{
	if (fs == NULL || fs->magic != TT_BOOT_FS_NG_MAGIC || !device_is_ready(fs->dev)) {
		return 1;
	}
	return 0;
}

int tt_bootfs_ng_read(const tt_boot_fs_ng *fs, uint32_t addr, uint8_t *buffer, size_t size)
{
	if (tt_bootfs_ng_initial_check(fs) != 0) {
		return TT_BOOT_FS_ERR;
	}

	return flash_read(fs->dev, addr, buffer, size);
}

int tt_bootfs_ng_write(const tt_boot_fs_ng *fs, uint32_t addr, const uint8_t *buffer, size_t size)
{
	if (tt_bootfs_ng_initial_check(fs) != 0) {
		return TT_BOOT_FS_ERR;
	}

	if (!buffer) {
		return TT_BOOT_FS_ERR;
	}

	return flash_write(fs->dev, addr, buffer, size);
}

int tt_bootfs_ng_erase(const tt_boot_fs_ng *fs, uint32_t addr, size_t size)
{
	if (tt_bootfs_ng_initial_check(fs) != 0) {
		return TT_BOOT_FS_ERR;
	}

	if (size == 0) {
		return TT_BOOT_FS_ERR;
	}

	return flash_erase(fs->dev, addr, size);
}

/* Allocate new file descriptor on SPI device and write associated data to correct address */
int tt_bootfs_ng_add_file(const tt_boot_fs_ng *fs, tt_boot_fs_fd fd, const uint8_t *image_data_src,
			  bool isFailoverEntry, bool isSecurityBinaryEntry)
{
	if (tt_bootfs_ng_initial_check(fs) != 0) {
		return TT_BOOT_FS_ERR;
	}

	uint32_t curr_fd_addr;

	/* Failover image has specific file descriptor location (BOOT_START + DESC_REGION_SIZE) */
	if (isFailoverEntry) {
		curr_fd_addr = TT_BOOT_FS_FAILOVER_HEAD_ADDR;
	} else if (isSecurityBinaryEntry) {
		curr_fd_addr = TT_BOOT_FS_SECURITY_BINARY_FD_ADDR;
	} else {
		/* Regular file descriptor - find first invalid slot */
		tt_boot_fs_fd head = {0};

		curr_fd_addr = TT_BOOT_FS_FD_HEAD_ADDR;

		tt_bootfs_ng_read(fs, TT_BOOT_FS_FD_HEAD_ADDR, (uint8_t *)&head,
				  sizeof(tt_boot_fs_fd));

		/* Traverse until we find an invalid file descriptor entry in SPI device array */
		while (head.flags.f.invalid == 0) {
			curr_fd_addr = tt_boot_fs_next(curr_fd_addr);
			tt_bootfs_ng_read(fs, curr_fd_addr, (uint8_t *)&head,
					  sizeof(tt_boot_fs_fd));
		}
	}

	if (tt_bootfs_ng_write(fs, curr_fd_addr, (uint8_t *)&fd, sizeof(tt_boot_fs_fd)) != 0) {
		return TT_BOOT_FS_ERR;
	}

	/*
	 * Now copy total image size from image_data_src pointer into the specified address.
	 * Total image size = image_size + signature_size (security) + padding.
	 */
	uint32_t total_image_size = fd.flags.f.image_size + fd.security_flags.f.signature_size;

	if (tt_bootfs_ng_write(fs, fd.spi_addr, image_data_src, total_image_size) != 0) {
		return TT_BOOT_FS_ERR;
	}

	return TT_BOOT_FS_OK;
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

int tt_boot_fs_ls(const tt_boot_fs_ng *fs, tt_boot_fs_fd *fds, size_t nfds)
{
	if (tt_bootfs_ng_initial_check(fs) != 0) {
		return TT_BOOT_FS_ERR;
	}

	if (!fds || nfds == 0) {
		return TT_BOOT_FS_ERR;
	}

	size_t count = 0;
	uint32_t addr = TT_BOOT_FS_FD_HEAD_ADDR;

	while (count < nfds) {
		tt_boot_fs_fd fd;

		if (tt_bootfs_ng_read(fs, addr, (uint8_t *)&fd, sizeof(tt_boot_fs_fd)) != 0) {
			break;
		}

		if (fd.flags.f.invalid) {
			break;
		}

		tt_checksum_res_t chk_res = calculate_and_compare_checksum(
			(uint8_t *)&fd, sizeof(tt_boot_fs_fd) - sizeof(uint32_t), fd.fd_crc, false);

		if (chk_res == TT_BOOT_FS_CHK_OK) {
			fds[count] = fd;
			count++;
		}

		addr = tt_boot_fs_next(addr);

		/* Safety check: don't scan past security binary address */
		if (addr >= TT_BOOT_FS_SECURITY_BINARY_FD_ADDR) {
			break;
		}
	}
	return count;
}

int tt_bootfs_ng_find_fd_by_tag(const tt_boot_fs_ng *fs, const uint8_t *tag, tt_boot_fs_fd *fd_data)
{
	/* Use the new cacheless approach to get the list of all fds */
	tt_boot_fs_fd fds[32];
	int fd_count = tt_boot_fs_ls(fs, fds, ARRAY_SIZE(fds));

	if (fd_count < 0) {
		return TT_BOOT_FS_ERR;
	}

	for (int i = 0; i < fd_count; i++) {
		if (memcmp(fds[i].image_tag, tag, TT_BOOT_FS_IMAGE_TAG_SIZE) == 0) {
			*fd_data = fds[i];
			return TT_BOOT_FS_OK;
		}
	}

	/* File descriptor not found */
	return TT_BOOT_FS_ERR;
}

int tt_bootfs_ng_get_file(const tt_boot_fs_ng *fs, const uint8_t *tag, uint8_t *buf,
			  size_t buf_size, size_t *file_size)
{
	if (fs == NULL || fs->magic != TT_BOOT_FS_NG_MAGIC || tag == NULL || buf == NULL ||
	    file_size == NULL || buf_size == 0) {
		return TT_BOOT_FS_ERR;
	}

	tt_boot_fs_fd fd_data;

	/* Find the file with the matching tag */
	if (tt_bootfs_ng_find_fd_by_tag(fs, tag, &fd_data) != TT_BOOT_FS_OK) {
		return TT_BOOT_FS_ERR;
	}

	if (fd_data.flags.f.image_size > buf_size) {
		return TT_BOOT_FS_ERR;
	}
	*file_size = fd_data.flags.f.image_size;

	if (tt_bootfs_ng_read(fs, fd_data.spi_addr, buf, fd_data.flags.f.image_size) != 0) {
		return TT_BOOT_FS_ERR;
	}

	if (calculate_and_compare_checksum(buf, fd_data.flags.f.image_size, fd_data.data_crc,
					   false) != TT_BOOT_FS_CHK_OK) {
		return TT_BOOT_FS_ERR;
	}

	return TT_BOOT_FS_OK;
}
