/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(tt_boot_fs, LOG_LEVEL_DBG);

tt_boot_fs boot_fs_data;
static tt_boot_fs_fd boot_fs_cache[16];

uint32_t tt_boot_fs_next(uint32_t last_fd_addr)
{
	return (last_fd_addr + sizeof(tt_boot_fs_fd));
}

static int tt_boot_fs_load_cache(tt_boot_fs *tt_boot_fs)
{
	if (tt_boot_fs == NULL) {
		LOG_ERR("tt_boot_fs pointer is NULL");
		return TT_BOOT_FS_ERR;
	}

	if (tt_boot_fs->hal_spi_read_f == NULL) {
		LOG_ERR("hal_spi_read_f is NULL");
		return TT_BOOT_FS_ERR;
	}

	if (tt_boot_fs->hal_spi_read_f(TT_BOOT_FS_FD_HEAD_ADDR, sizeof(boot_fs_cache),
				       (uint8_t *)boot_fs_cache) != TT_BOOT_FS_OK) {
		LOG_ERR("Failed to read boot fs cache");
		return TT_BOOT_FS_ERR;
	}

	return TT_BOOT_FS_OK;
}

/* Sets up hardware abstraction layer (HAL) callbacks, initializes HEAD fd */
int tt_boot_fs_mount(tt_boot_fs *tt_boot_fs, tt_boot_fs_read hal_read, tt_boot_fs_write hal_write,
		     tt_boot_fs_erase hal_erase)
{
	tt_boot_fs->hal_spi_read_f = hal_read;
	tt_boot_fs->hal_spi_write_f = hal_write;
	tt_boot_fs->hal_spi_erase_f = hal_erase;

	return tt_boot_fs_load_cache(tt_boot_fs);
}

/* Allocate new file descriptor on SPI device and write associated data to correct address */
int tt_boot_fs_add_file(const tt_boot_fs *tt_boot_fs, tt_boot_fs_fd fd,
			const uint8_t *image_data_src, bool isFailoverEntry,
			bool isSecurityBinaryEntry)
{
	uint32_t curr_fd_addr;

	/* Failover image has specific file descriptor location (BOOT_START + DESC_REGION_SIZE) */
	if (isFailoverEntry) {
		curr_fd_addr = TT_BOOT_FS_FAILOVER_HEAD_ADDR;
	} else if (isSecurityBinaryEntry) {
		curr_fd_addr = TT_BOOT_FS_SECURITY_BINARY_FD_ADDR;
	} else {
		/* Regular file descriptor */
		tt_boot_fs_fd head = {0};

		curr_fd_addr = TT_BOOT_FS_FD_HEAD_ADDR;

		tt_boot_fs->hal_spi_read_f(TT_BOOT_FS_FD_HEAD_ADDR, sizeof(tt_boot_fs_fd),
					   (uint8_t *)&head);

		/* Traverse until we find an invalid file descriptor entry in SPI device array */
		while (head.flags.f.invalid == 0) {
			curr_fd_addr = tt_boot_fs_next(curr_fd_addr);
			tt_boot_fs->hal_spi_read_f(curr_fd_addr, sizeof(tt_boot_fs_fd),
						   (uint8_t *)&head);
		}
	}

	tt_boot_fs->hal_spi_write_f(curr_fd_addr, sizeof(tt_boot_fs_fd), (uint8_t *)&fd);

	/*
	 * Now copy total image size from image_data_src pointer into the specified address.
	 * Total image size = image_size + signature_size (security) + padding.
	 */
	uint32_t total_image_size = fd.flags.f.image_size + fd.security_flags.f.signature_size;

	tt_boot_fs->hal_spi_write_f(fd.spi_addr, total_image_size, image_data_src);

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

static int find_fd_by_tag(const tt_boot_fs *tt_boot_fs, const uint8_t *tag, tt_boot_fs_fd *fd_data)
{
	for (uint32_t i = 0; i < ARRAY_SIZE(boot_fs_cache); i++) {
		if (boot_fs_cache[i].flags.f.invalid) {
			continue;
		}

		if (memcmp(boot_fs_cache[i].image_tag, tag, TT_BOOT_FS_IMAGE_TAG_SIZE) != 0) {
			continue;
		}

		tt_checksum_res_t chk_res = calculate_and_compare_checksum(
			(uint8_t *)&boot_fs_cache[i], sizeof(tt_boot_fs_fd) - sizeof(uint32_t),
			boot_fs_cache[i].fd_crc, false);

		if (chk_res == TT_BOOT_FS_CHK_FAIL) {
			continue;
		}

		/* Found the right file descriptor */
		*fd_data = boot_fs_cache[i];
		return TT_BOOT_FS_OK;
	}

	/* File descriptor not found */
	return TT_BOOT_FS_ERR;
}

int tt_boot_fs_get_file(const tt_boot_fs *tt_boot_fs, const uint8_t *tag, uint8_t *buf,
			size_t buf_size, size_t *file_size)
{
	tt_boot_fs_fd fd_data;

	if (tt_boot_fs == NULL || tag == NULL || buf == NULL || file_size == NULL) {
		return TT_BOOT_FS_ERR;
	}

	if (find_fd_by_tag(tt_boot_fs, tag, &fd_data) != TT_BOOT_FS_OK) {
		return TT_BOOT_FS_ERR;
	}

	if (fd_data.flags.f.image_size > buf_size) {
		return TT_BOOT_FS_ERR;
	}
	*file_size = fd_data.flags.f.image_size;

	tt_boot_fs->hal_spi_read_f(fd_data.spi_addr, fd_data.flags.f.image_size, buf);
	if (calculate_and_compare_checksum(buf, fd_data.flags.f.image_size, fd_data.data_crc,
					   false) != TT_BOOT_FS_CHK_OK) {
		return TT_BOOT_FS_ERR;
	}

	return TT_BOOT_FS_OK;
}

/* The items below are stop-gap until there is a new tt boot fs api */

static const struct device *flash_dev;

static int z_tt_boot_fs_read(uint32_t addr, uint32_t size, uint8_t *dst)
{
	int ret;

	LOG_INF("calling %s(%p, %p, %p, %u)", "flash_read", flash_dev, (void *)addr, (void *)dst,
		size);
	k_msleep(500);
	ret = flash_read(flash_dev, addr, dst, size);
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "flash_read", ret);
		k_msleep(500);
	}

	return ret;
}

static int z_tt_boot_fs_write(uint32_t addr, uint32_t size, const uint8_t *src)
{
	int ret;

	ret = flash_write(flash_dev, addr, src, size);
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "flash_write", ret);
	}

	return ret;
}

static int z_tt_boot_fs_erase(uint32_t addr, uint32_t size)
{
	int ret;

	ret = flash_erase(flash_dev, addr, size);
	if (ret < 0) {
		LOG_ERR("%s() failed: %d", "flash_erase", ret);
	}

	return ret;
}

int tt_boot_fs_mount_by_device(const struct device *dev)
{
	if (!device_is_ready(dev)) {
		LOG_ERR("Device is not ready");
		return -ENODEV;
	}

	flash_dev = dev;
	boot_fs_data.hal_spi_read_f = z_tt_boot_fs_read;
	boot_fs_data.hal_spi_write_f = z_tt_boot_fs_write;
	boot_fs_data.hal_spi_erase_f = z_tt_boot_fs_erase;

	LOG_INF("loading boot fs cache");
	return tt_boot_fs_load_cache(&boot_fs_data) == TT_BOOT_FS_OK ? 0 : -EIO;
}
