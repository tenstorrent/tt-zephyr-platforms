/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#define MCUBOOT_PART_NODE  DT_NODE_BY_FIXED_PARTITION_LABEL(mcuboot)
#define BLUPDATE_PART_NODE DT_NODE_BY_FIXED_PARTITION_LABEL(blupdate)

BUILD_ASSERT(DT_FIXED_PARTITION_EXISTS(MCUBOOT_PART_NODE),
	     "No mcuboot partition found in devicetree");
BUILD_ASSERT(DT_FIXED_PARTITION_EXISTS(BLUPDATE_PART_NODE),
	     "No blupdate partition found in devicetree");

uint8_t flash_copy_buf[4 * 1024] __aligned(4);

int main(void)
{
	const struct device *tgt_flash = FIXED_PARTITION_NODE_DEVICE(MCUBOOT_PART_NODE);
	const struct device *src_flash = FIXED_PARTITION_NODE_DEVICE(BLUPDATE_PART_NODE);
	int rc;
	off_t tgt_off, src_off;
	size_t len;

	/*
	 * Simply copy the flash data from the bl_update partition to
	 * the MCUBoot partition. There isn't really a recovery path here
	 * if something goes wrong...
	 */
	printk("Starting DMFW rom update...\n");
	tgt_off = DT_REG_ADDR(MCUBOOT_PART_NODE);
	src_off = DT_REG_ADDR(BLUPDATE_PART_NODE);
	len = FIXED_PARTITION_NODE_SIZE(BLUPDATE_PART_NODE);

	printk("Erasing flash at 0x%lx, size 0x%zx\n", tgt_off, len);
	rc = flash_erase(tgt_flash, tgt_off, len);
	if (rc != 0) {
		printk("Flash erase failed: %d\n", rc);
		return rc;
	}
	printk("Copying 0x%zx bytes from 0x%lx to 0x%lx\n", len, src_off, tgt_off);
	rc = flash_copy(src_flash, src_off, tgt_flash, tgt_off, len, flash_copy_buf,
			sizeof(flash_copy_buf));
	if (rc != 0) {
		printk("Flash copy failed: %d\n", rc);
		return rc;
	}
	/*
	 * Manually mark slot0 as confirmed. We don't actually intend to
	 * boot slot0 (it is now invalid in the BL2 scheme), but if MCUBoot
	 * doesn't see a valid header, it won't swap in slot1 from SPI flash.
	 */
	rc = boot_write_img_confirmed();
	if (rc != 0) {
		printk("boot_write_img_confirmed failed: %d\n", rc);
		return rc;
	}
	rc = boot_request_upgrade(BOOT_UPGRADE_PERMANENT);
	if (rc != 0) {
		printk("boot_request_upgrade failed: %d\n", rc);
		return rc;
	}
	printk("DMFW rom update complete\n");
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}
