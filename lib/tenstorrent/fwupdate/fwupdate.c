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
#define ERASE_BLOCK_SIZE  DT_PROP(DT_NODELABEL(flash0), erase_block_size)
#define WRITE_BLOCK_SIZE  DT_PROP(DT_NODELABEL(flash0), write_block_size)

/* The external SPI flash is not partitioned, so the image begins at 0 */
#define TT_BOOT_FS_OFFSET 0

int tt_fwupdate_init(const struct device *dev, struct gpio_dt_spec mux)
{
	ARG_UNUSED(mux);
	ARG_UNUSED(dev);

	return 0;
}

int tt_fwupdate_complete(void)
{
	return 0;
}
#endif

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

	ARG_UNUSED(tag);

	BUILD_ASSERT((sizeof(fake_image) % sizeof(uint32_t)) == 0);

	rc = flash_write(flash1_dev, DT_REG_ADDR(DT_NODELABEL(slot1_partition)), &fake_image,
			 sizeof(fake_image));
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "flash_write", rc);
		return rc;
	}

	return 0;
}
#endif

int tt_fwupdate(const char *tag, bool dry_run, bool reboot)
{
	ARG_UNUSED(tag);

	if (!dry_run) {
		int rc;

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
	ARG_UNUSED(fd);

	/* This is a no-op because mcuboot does all of it for us */
	return 0;
}

int tt_fwupdate_is_confirmed(void)
{
	return (int)boot_is_img_confirmed();
}

int tt_fwupdate_validate_fd(const tt_boot_fs_fd *fd)
{
	ARG_UNUSED(fd);

	/* This is a no-op because mcuboot does all of it for us */
	return 0;
}
