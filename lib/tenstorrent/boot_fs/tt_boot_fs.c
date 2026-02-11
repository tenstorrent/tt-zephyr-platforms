/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tt_boot_fs, CONFIG_TT_APP_LOG_LEVEL);

tt_boot_fs boot_fs_data;

uint32_t tt_boot_fs_next(uint32_t last_fd_addr)
{
	return (last_fd_addr + sizeof(tt_boot_fs_fd));
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

int tt_boot_fs_ls(const struct device *dev, tt_boot_fs_fd *fds, size_t nfds, size_t offset)
{
	if (!dev || !device_is_ready(dev)) {
		return -ENXIO;
	}

	int ret;
	tt_boot_fs_header header;

	if (nfds == 0) {
		return 0;
	}

	size_t found = 0;
	size_t i = 0;
	size_t header_addr = TT_BOOT_FS_HEADER_ADDR;
	size_t header_end;
	uint32_t fd_addr;

	/* Read FD headers */
	ret = flash_read(dev, header_addr, &header, sizeof(header));
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "flash_read", ret);
		return -EIO;
	}
	if (header.magic != TT_BOOT_FS_MAGIC) {
		LOG_ERR("Invalid boot FS magic: 0x%08X", header.magic);
		return -ENXIO;
	}
	if (header.version != TT_BOOT_FS_CURRENT_VERSION) {
		LOG_ERR("Unsupported boot FS version: %d", header.version);
		return -ENXIO;
	}
	header_end = TT_BOOT_FS_HEADER_ADDR + sizeof(tt_boot_fs_header) +
		     header.table_count * sizeof(uint32_t);
	header_addr += sizeof(tt_boot_fs_header);

	while (header_addr < header_end) {

		/* Read address of this table */
		ret = flash_read(dev, header_addr, &fd_addr, sizeof(uint32_t));
		if (ret < 0) {
			LOG_ERR("%s() failed: %d", "flash_read", ret);
			return -EIO;
		}

		while (found < nfds) {
			tt_boot_fs_fd fd;

			ret = flash_read(dev, fd_addr, &fd, sizeof(tt_boot_fs_fd));
			if (ret < 0) {
				LOG_ERR("%s() failed: %d", "flash_read", ret);
				return -EIO;
			}

			if (fd.flags.f.invalid) {
				break;
			}

			ret = calculate_and_compare_checksum(
				(uint8_t *)&fd, sizeof(tt_boot_fs_fd) - sizeof(uint32_t), fd.fd_crc,
				false);
			if (ret != TT_BOOT_FS_CHK_OK) {
				return -ENXIO;
			}

			if (i >= offset) {
				if (fds != NULL && found < nfds) {
					fds[found] = fd;
				}
				found++;
				if (found == nfds) {
					break;
				}
			}
			i++;
			fd_addr += sizeof(tt_boot_fs_fd);
		}

		header_addr += sizeof(uint32_t);
	}

	return found;
}

int tt_boot_fs_find_fd_by_tag(const struct device *flash_dev, const uint8_t *tag, tt_boot_fs_fd *fd)
{
	if (tag == NULL) {
		return -EINVAL;
	}

	int ret;

	tt_boot_fs_fd fds[CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX];

	ret = tt_boot_fs_ls(flash_dev, fds, ARRAY_SIZE(fds), 0);

	if (ret < 0) {
		return ret;
	}

	ARRAY_FOR_EACH(fds, i) {
		if (i >= ret) {
			break;
		}

		if (strncmp(tag, fds[i].image_tag, sizeof(fds[i].image_tag)) == 0) {
			if (fd != NULL) {
				*fd = fds[i];
			}
			return 0;
		}
	}
	return -ENOENT;
}
