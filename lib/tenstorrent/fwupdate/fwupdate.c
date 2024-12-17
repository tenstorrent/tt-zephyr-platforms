/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <tenstorrent/fwupdate.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <tenstorrent/bh_chip.h>

#ifndef IMAGE_MAGIC
#define IMAGE_MAGIC 0x96f3b83d
#endif

/* d is LSByte */
#define AS_U32(a, b, c, d)                                                                         \
	(((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) |                    \
	 ((uint32_t)(d) << 0))

LOG_MODULE_REGISTER(tt_fwupdate, CONFIG_TT_FWUPDATE_LOG_LEVEL);

static tt_boot_fs boot_fs;

#ifdef CONFIG_BOARD_QEMU_X86
#define FLASH0_NODE DT_INST(0, zephyr_sim_flash)
#define FLASH1_NODE FLASH0_NODE
#define ERASE_BLOCK_SIZE DT_PROP(DT_NODELABEL(flash_sim0), erase_block_size)
#define WRITE_BLOCK_SIZE DT_PROP(DT_NODELABEL(flash_sim0), write_block_size)

static const struct device *const flash1_dev = DEVICE_DT_GET(FLASH1_NODE);
/* For testing, we construct a fake image in slot0_partition, which may not be at offset 0 */
#define TT_BOOT_FS_OFFSET DT_REG_ADDR(DT_NODELABEL(storage_partition))
#else
#define FLASH0_NODE       DT_NODELABEL(flash) /* "flash" (NOT "flash0") is internal flash */
#define ERASE_BLOCK_SIZE DT_PROP(DT_NODELABEL(flash0), erase_block_size)
#define WRITE_BLOCK_SIZE DT_PROP(DT_NODELABEL(flash0), write_block_size)

static const struct device *flash1_dev;
static struct gpio_dt_spec spi_mux;

/* The external SPI flash is not partitioned, so the image begins at 0 */
#define TT_BOOT_FS_OFFSET 0

int tt_fwupdate_init(const struct device *dev, struct gpio_dt_spec mux)
{
	int ret;

	flash1_dev = dev;
	spi_mux = mux;

	if (spi_mux.port != NULL) {
		ret = gpio_pin_configure_dt(&spi_mux, GPIO_OUTPUT_INACTIVE);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int tt_fwupdate_complete(void)
{
	int ret;

	if (spi_mux.port != NULL) {
		ret = gpio_pin_set_dt(&spi_mux, 1);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}
#endif

static const struct device *const flash0_dev = DEVICE_DT_GET(FLASH0_NODE);
/* Should just be an enable on the flash/spi */

static int z_tt_boot_fs_read(uint32_t addr, uint32_t size, uint8_t *dst)
{
	return flash_read(flash1_dev, addr, dst, size);
}

static int z_tt_boot_fs_write(uint32_t addr, uint32_t size, const uint8_t *src)
{
	return flash_write(flash1_dev, addr, src, size);
}

static int z_tt_boot_fs_erase(uint32_t addr, uint32_t size)
{
	return flash_erase(flash1_dev, addr, size);
}

static void tt_fwupdate_dump_fd(const char *msg, const tt_boot_fs_fd *fd, bool verified)
{
	LOG_DBG("%s%s{spi_addr: %x, copy_dest: %x, flags: { image_size: %zu, executable: %d, "
		"invalid: "
		"%d}, data_crc: "
		"%x, security_flags: %x, image_tag: %.*s, fd_crc: %x%s}",
		(msg == NULL) ? "" : msg, (msg == NULL || msg[0] == '\0') ? "" : ": ", fd->spi_addr,
		fd->copy_dest, fd->flags.f.image_size, fd->flags.f.executable, fd->flags.f.invalid,
		fd->data_crc, fd->security_flags.val, TT_BOOT_FS_IMAGE_TAG_SIZE, fd->image_tag,
		fd->fd_crc, verified ? " (verified)" : "");
}

#ifdef CONFIG_TT_FWUPDATE_TEST
static const uint32_t fake_image[] = {
	/* start of 16-byte mcuboot header */
	IMAGE_MAGIC,
	0x0,
	0x0,
	0x0,
	/* end of 16-byte mcuboot header */
	0x03020100,
	0x07060504,
	0x0b0a0908,
	0x0f0e0d0c,
};

int tt_fwupdate_create_test_fs(const char *tag)
{
	int rc;
	tt_boot_fs_fd tmp;
	tt_boot_fs_fd fd = {
		.spi_addr = TT_BOOT_FS_OFFSET + sizeof(tt_boot_fs_fd),
		.data_crc = tt_boot_fs_cksum(0, (const uint8_t *)fake_image, sizeof(fake_image)),
		.flags.f.image_size = sizeof(fake_image),
	};

	BUILD_ASSERT((sizeof(fake_image) % sizeof(uint32_t)) == 0);

	strncpy(fd.image_tag, tag, sizeof(fd.image_tag));
	fd.fd_crc = tt_boot_fs_cksum(0, (uint8_t *)&fd, sizeof(tt_boot_fs_fd) - sizeof(uint32_t));

	/* Create a tiny, fake image */
	rc = tt_boot_fs_mount(&boot_fs, z_tt_boot_fs_read, z_tt_boot_fs_write, z_tt_boot_fs_erase);
	if (rc != TT_BOOT_FS_OK) {
		LOG_ERR("%s() failed: %d", "tt_boot_fs_mount", rc);
		return -EIO;
	}

	/* FIXME: tt_boot_fs_add_file() image_data_src should be const */
	rc = tt_boot_fs_add_file(&boot_fs, fd, (uint8_t *)fake_image, false, false);
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "tt_boot_fs_add_file", rc);
		return rc;
	}

	rc = flash_write(flash1_dev, TT_BOOT_FS_OFFSET, &fd, sizeof(fd));
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "flash_write", rc);
		return rc;
	}

	rc = flash_read(flash1_dev, TT_BOOT_FS_OFFSET, &tmp, sizeof(tmp));
	__ASSERT(rc == 0, "%s() failed: %d", "flash_read", rc);
	__ASSERT(memcmp(&fd, &tmp, sizeof(fd)) == 0,
		 "Written and read back file descriptors do not match");

	tt_fwupdate_dump_fd("Created fd: ", &fd, false);

	return 0;
}
#endif

int tt_fwupdate(const char *tag, bool dry_run, bool reboot)
{
	int rc;
	uint32_t cksum;
	tt_boot_fs_fd fd;
	bool found = false;

	rc = tt_boot_fs_mount(&boot_fs, z_tt_boot_fs_read, z_tt_boot_fs_write, z_tt_boot_fs_erase);
	if (rc != TT_BOOT_FS_OK) {
		LOG_DBG("%s() failed: %d", "tt_boot_fs_mount", rc);
		return -EIO;
	}

	if (!device_is_ready(flash1_dev)) {
		LOG_DBG("Device %s is not ready", flash1_dev->name);
		return -ENODEV;
	}

	LOG_DBG("Parsing SPI flash %s", flash1_dev->name);

	for (uint32_t addr = TT_BOOT_FS_OFFSET + TT_BOOT_FS_FD_HEAD_ADDR, i = 0;
	     i < CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX; addr = tt_boot_fs_next(addr), ++i) {

		rc = flash_read(flash1_dev, addr, (uint8_t *)&fd, sizeof(tt_boot_fs_fd));
		if (rc < 0) {
			LOG_DBG("%s() failed: %d", "flash_read", rc);
			break;
		}

		rc = tt_fwupdate_validate_fd(&fd);
		if (rc < 0) {
			break;
		}
		if (strncmp(fd.image_tag, tag, sizeof(fd.image_tag)) != 0) {
			continue;
		}

		rc = tt_fwupdate_validate_image(&fd);
		if (rc < 0) {
			continue;
		}

		found = true;
		break;
	}

	if (!found) {
		LOG_DBG("Did not find image tag %s", tag);
		return -ENOENT;
	}

	tt_fwupdate_dump_fd("Found fd", &fd, true);

	/*
	 * Alpha firmware had no means of getting signaled to initiate a firmware update from the
	 * host. In that scenario, the only means of updating is to overwrite if the new image is
	 * different.
	 */
#ifdef CONFIG_TT_FWUPDATE_TEST
	cksum = tt_boot_fs_cksum(0, (const uint8_t *)fake_image, sizeof(fake_image));
#else
	cksum = tt_boot_fs_cksum(
		0,
		(const uint8_t *)(uintptr_t)(DT_REG_ADDR(DT_NODELABEL(flash0)) +
					     DT_REG_ADDR(DT_NODELABEL(slot0_partition))),
		fd.flags.f.image_size);
#endif

	LOG_DBG("slot0_partition has checksum %08x", cksum);

	if (cksum == fd.data_crc) {
		/*
		 * do not write image to slot1 or update if it is equal to the slot0 image
		 * this avoids a boot loop when 'reset' is true.
		 */
		LOG_DBG("Image %s is identical to that of slot0_partition", tag);
		return 0;
	}

#ifndef CONFIG_TT_FWUPDATE_TEST
	cksum = tt_boot_fs_cksum(
		0,
		(const uint8_t *)(uintptr_t)(DT_REG_ADDR(DT_NODELABEL(flash0)) +
					     DT_REG_ADDR(DT_NODELABEL(slot1_partition))),
		fd.flags.f.image_size);
#endif

	LOG_DBG("slot1_partition has checksum %08x", cksum);

	if (cksum == fd.data_crc) {
		/*
		 * Also do not write an update to slot1 if the existing slot1 image is identical to
		 * the update.
		 */
		LOG_DBG("Image %s is identical to that of slot1_partition", tag);
		return 0;
	}

	if (!dry_run) {
		rc = tt_fwupdate_flash_image(&fd);
		if (rc < 0) {
			LOG_ERR("%s() failed: %d", "tt_fwupdate_flash_image", rc);
			return -EIO;
		}

		rc = boot_request_upgrade(BOOT_UPGRADE_TEST);
		if (rc < 0) {
			LOG_ERR("%s() failed: %d", "boot_request_upgrade", rc);
			return rc;
		}

		if (reboot && IS_ENABLED(CONFIG_REBOOT)) {
			LOG_INF("Rebooting...\r\n\r\n");
			sys_reboot(SYS_REBOOT_COLD);
		}
	}

	return 1;
}

int tt_fwupdate_confirm(void)
{
	int rc;

	if (!boot_is_img_confirmed()) {
		rc = boot_write_img_confirmed();
		if (rc < 0) {
			LOG_DBG("%s() failed: %d", "boot_write_img_confirmed", rc);
			return rc;
		}
	}

	LOG_INF("Firmware update is confirmed.");

	return 0;
}

int tt_fwupdate_flash_image(const tt_boot_fs_fd *fd)
{
	int rc;
	uint8_t write_buf[CONFIG_TT_FWUPDATE_WRITE_BUF_SIZE];
	size_t write_size = ROUND_UP(fd->flags.f.image_size,
				     WRITE_BLOCK_SIZE);
	size_t erase_size = ROUND_UP(fd->flags.f.image_size,
				     ERASE_BLOCK_SIZE);

	__ASSERT(write_size <= sizeof(write_buf), "write_size %zu exceeds sizeof(write_buf) %zu",
		 write_size, sizeof(write_buf));

	if (erase_size >= DT_REG_SIZE(DT_NODELABEL(slot1_partition))) {
		LOG_DBG("erase size %zu exceeds partition size %zu", erase_size,
			DT_REG_SIZE(DT_NODELABEL(slot1_partition)));
		return -EFBIG;
	}

	rc = flash_erase(flash0_dev, DT_REG_ADDR(DT_NODELABEL(slot1_partition)), erase_size) ||
	     flash_copy(flash1_dev, fd->spi_addr, flash0_dev,
			DT_REG_ADDR(DT_NODELABEL(slot1_partition)), write_size,
			write_buf, sizeof(write_buf));
	if (rc < 0) {
		LOG_DBG("flash_erase() or flash_copy() failed: %d", rc);
		return rc;
	}

	return 0;
}

int tt_fwupdate_is_confirmed(void)
{
	return (int)boot_is_img_confirmed();
}

int tt_fwupdate_validate_fd(const tt_boot_fs_fd *fd)
{
	uint32_t cksum;

	if (fd == NULL) {
		return -EINVAL;
	}

	if (fd->flags.f.invalid) {
		LOG_DBG("fd invalid bit is set");
		return -EINVAL;
	}

	/* FIXME: we should really use a standard CRC32 algorithm here */
	cksum = tt_boot_fs_cksum(0, (uint8_t *)fd, sizeof(tt_boot_fs_fd) - sizeof(uint32_t));
	if (cksum != fd->fd_crc) {
		tt_fwupdate_dump_fd("Invalid fd", fd, false);
		LOG_DBG("fd_crc mismatch: actual: %08x expected: %08x", cksum, fd->fd_crc);
		return -ENOENT;
	}

	return 0;
}

/*
 * Might be good to create a "for each chunk" function that properly iterates through the image,
 * calling a callback on each iteration. Validation, flashing, and probably read-back could use
 * such a function, and it would prevent some code duplication.
 */
int tt_fwupdate_validate_image(const tt_boot_fs_fd *fd)
{
	int rc;
	uint32_t magic = 0;
	uint32_t cksum = 0;
	uint8_t cksum_buf[128];

	if (fd == NULL) {
		return -EINVAL;
	}

	if (fd->flags.f.image_size > DT_REG_SIZE(DT_NODELABEL(slot1_partition))) {
		LOG_ERR("image size %zu is too large for slot1-partition size %zu",
			fd->flags.f.image_size, DT_REG_SIZE(DT_NODELABEL(slot1_partition)));
		return -ENOENT;
	}

	/* Ensure that IMAGE_MAGIC is found in the first 4 bytes of the image, otherwise it will not
	 * be bootable
	 */
	LOG_DBG("reading mcuboot header from %s offset %x", flash1_dev->name, fd->spi_addr);
	rc = flash_read(flash1_dev, fd->spi_addr, cksum_buf, 4);
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "flash_read", rc);
		return -EIO;
	}

	magic = AS_U32(cksum_buf[3], cksum_buf[2], cksum_buf[1], cksum_buf[0]);
	if (magic != IMAGE_MAGIC) {
		LOG_ERR("magic %08x not equal to IMAGE_MAGIC (%08x)", magic, IMAGE_MAGIC);
		return -EIO;
	}

	for (size_t offs = 0, addr = fd->spi_addr, N = fd->flags.f.image_size, bytes_read = 0,
		    bytes_left = N;
	     offs < N; offs += bytes_read, bytes_left -= bytes_read, addr += bytes_read) {

		bytes_read = MIN(bytes_left, sizeof(cksum_buf));

		rc = flash_read(flash1_dev, addr, cksum_buf, bytes_read);
		if (rc < 0) {
			LOG_ERR("%s() failed: %d", "flash_read", rc);
			return -EIO;
		}

		cksum = tt_boot_fs_cksum(cksum, cksum_buf, bytes_read);
	}

	if (cksum != fd->data_crc) {
		LOG_ERR("data_crc mismatch: actual: %08x expected: %08x", cksum, fd->data_crc);
		return -ENOENT;
	}

	LOG_INF("verified bmfw with checksum %08x \\o/", cksum);

	return 0;
}
