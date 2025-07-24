/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/devicetree.h>
#include <string.h>
#include <tenstorrent/tt_boot_fs.h>

#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_flash_controller), okay)
#define FLASH_DEVICE           DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))
#define FLASH_DEVICE_AVAILABLE 1
#elif DT_NODE_EXISTS(DT_NODELABEL(spi_flash)) && DT_NODE_HAS_STATUS(DT_NODELABEL(spi_flash), okay)
#define FLASH_DEVICE           DEVICE_DT_GET(DT_NODELABEL(spi_flash))
#define FLASH_DEVICE_AVAILABLE 1
#elif DT_NODE_EXISTS(DT_NODELABEL(flash0)) && DT_NODE_HAS_STATUS(DT_NODELABEL(flash0), okay)
#define FLASH_DEVICE           DEVICE_DT_GET(DT_NODELABEL(flash0))
#define FLASH_DEVICE_AVAILABLE 1
#elif DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_flash), okay)
#define FLASH_DEVICE           DEVICE_DT_GET(DT_CHOSEN(zephyr_flash))
#define FLASH_DEVICE_AVAILABLE 1
#else
#define FLASH_DEVICE_AVAILABLE 0
#endif

ZTEST(tt_boot_fs, test_flash_device_debug)
{
#if FLASH_DEVICE_AVAILABLE
	printk("FLASH_DEVICE: %p\n", FLASH_DEVICE);
	printk("Flash device name: %s\n", FLASH_DEVICE->name);
	printk("Flash device ready: %s\n", device_is_ready(FLASH_DEVICE) ? "YES" : "NO");
#else
	printk("No flash device configured in device tree\n");
#endif
}

/* all input must be aligned to a 4-byte boundary and be a multiple of 4 bytes */
__aligned(sizeof(uint32_t)) static const uint8_t one_byte[] = {0x42};
/*
 * __aligned(sizeof(uint32_t)) static const uint8_t two_bytes[] = {
 *   0x42,
 *   0x42,
 * };
 * __aligned(sizeof(uint32_t)) static const uint8_t three_bytes[] = {
 *   0x73,
 *   0x42,
 *   0x42,
 * };
 */
static const uint32_t four_bytes = 0x42427373;
/*
 * __aligned(sizeof(uint32_t)) static const uint8_t five_bytes[] = {
 *   0x73, 0x73, 0x42, 0x42, 0x37,
 * };
 * __aligned(sizeof(uint32_t)) static const uint8_t six_bytes[] = {
 *   0x73, 0x73, 0x42, 0x42, 0x37, 0x37,
 * };
 * __aligned(sizeof(uint32_t)) static const uint8_t seven_bytes[] = {
 *   0x73, 0x73, 0x42, 0x42, 0x37, 0x37, 0x24,
 * };
 */
static const uint64_t eight_bytes = 0x2424373742427373;

static uint8_t test_buffer[256];
static tt_boot_fs_fd test_fds[CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX];

static void setup_test_fds(void)
{
	memset(test_fds, 0, sizeof(test_fds));

	/* Valid FD 1 */
	test_fds[0].spi_addr = 0x14000;
	test_fds[0].copy_dest = 0x1000000;
	test_fds[0].flags.f.invalid = 0;
	test_fds[0].data_crc = 0x80C1842F;
	test_fds[0].fd_crc = 0xF85C708D;
	memcpy(test_fds[0].image_tag, "cmfwcfg", strlen("cmfwcfg"));

	/* Valid FD 2 */
	test_fds[1].spi_addr = 0x15000;
	test_fds[1].copy_dest = 0x1000000;
	test_fds[1].flags.f.invalid = 0;
	test_fds[1].data_crc = 0x51FF9FC5;
	test_fds[1].fd_crc = 0xDB7E1100;
	memcpy(test_fds[1].image_tag, "cmfw", strlen("cmfw"));

	/* Valid FD 3 */
	test_fds[2].spi_addr = 0x49000;
	test_fds[2].copy_dest = 0x1000000;
	test_fds[2].flags.f.invalid = 0;
	test_fds[2].data_crc = 0x854D0C53;
	test_fds[2].fd_crc = 0x76595CCC;
	memcpy(test_fds[2].image_tag, "failover", strlen("failover"));

	test_fds[3].flags.f.invalid = 1; /* Mark the end of valid entries */
}

ZTEST(tt_boot_fs, test_tt_boot_fs_cksum)
{
	uint32_t cksum;

	static const struct harness_data {
		uint32_t expect;
		const uint8_t *data;
		size_t size;
	} harness[] = {
		{0, NULL, 0},
		{0, one_byte, 0},
		/*
		 * {0x00000042, one_byte, 1},
		 * {0x00004242, two_bytes, 2},
		 * {0x00000073, three_bytes, 3},
		 */
		{0x42427373, (uint8_t *)&four_bytes, 4},
		/*
		 *{0x4284e6e6, five_bytes, 5},
		 *{0x4242e6e6, six_bytes, 6},
		 *{0x424273e6, seven_bytes, 7},
		 */
		{0x6666aaaa, (uint8_t *)&eight_bytes, 8},
	};

	ARRAY_FOR_EACH_PTR(harness, it) {
		cksum = tt_boot_fs_cksum(0, it->data, it->size);
		zassert_equal(it->expect, cksum, "%d: expected: %08x actual: %08x", it - harness,
			      it->expect, cksum);
	}
}

ZTEST(tt_boot_fs, test_find_fd_by_tag_comprehensive)
{
	setup_test_fds();

	/* Test 1: valid tag with string literal */
	const tt_boot_fs_fd *result1 = tt_bootfs_ng_find_fd_by_tag("cmfw", test_fds, 4);

	zassert_not_null(result1, "Should find 'cmfw' tag");
	zassert_equal(result1, &test_fds[1], "Should return correct FD for 'cmfw'");
	zassert_mem_equal(result1->image_tag, "cmfw", strlen("cmfw"), "Tag should match");
	zassert_equal(result1->spi_addr, 0x15000, "SPI address should match");

	/* Test 2: valid tag with uint8_t array */
	const tt_boot_fs_fd *result2 = tt_bootfs_ng_find_fd_by_tag("failover", test_fds, 4);

	zassert_not_null(result2, "Should find 'failover' tag");
	zassert_equal(result2, &test_fds[2], "Should return correct FD for 'failover'");
	zassert_mem_equal(result2->image_tag, "failover", strlen("failover"), "Tag should match");
	zassert_equal(result2->spi_addr, 0x49000, "SPI address should match");

	/* Test 3: non-existent tag */
	const tt_boot_fs_fd *result3 = tt_bootfs_ng_find_fd_by_tag("missing", test_fds, 4);

	zassert_is_null(result3, "Should not find non-existent 'missing' tag");

	/* Test 4: empty tag */
	uint8_t search_tag[TT_BOOT_FS_IMAGE_TAG_SIZE];

	memset(search_tag, 0, TT_BOOT_FS_IMAGE_TAG_SIZE);
	const tt_boot_fs_fd *result4 = tt_bootfs_ng_find_fd_by_tag(search_tag, test_fds, 4);

	zassert_is_null(result4, "Should not find empty tag");

	/* Test 5: valid tag, single element array */
	const tt_boot_fs_fd *result5 = tt_bootfs_ng_find_fd_by_tag("cmfwcfg", test_fds, 1);

	zassert_not_null(result5, "Should find tag in single element array");
	zassert_equal(result5, &test_fds[0], "Should return first FD");
}

ZTEST(tt_boot_fs, test_ng_read_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	/* Test 1: NULL device */
	int result1 = tt_bootfs_ng_read(NULL, 0x1000, test_buffer, 100);

	zassert_equal(result1, TT_BOOT_FS_ERR, "Should return error for NULL device");

	if (device_is_ready(FLASH_DEVICE)) {
		/* Test 2: zero size */
		int result2 = tt_bootfs_ng_read(FLASH_DEVICE, 0x1000, test_buffer, 0);

		zassert_equal(result2, TT_BOOT_FS_OK, "Should succeed with zero size read");

		/* Test 3: valid parameters */
		int result3 =
			tt_bootfs_ng_read(FLASH_DEVICE, 0x0, test_buffer, sizeof(test_buffer));

		zassert_true(result3 == TT_BOOT_FS_OK || result3 != TT_BOOT_FS_ERR,
			     "Should not return TT_BOOT_FS_ERR with valid parameters");
	}
}

ZTEST(tt_boot_fs, test_ng_write_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	/* Test 1: NULL device*/
	int result1 = tt_bootfs_ng_write(NULL, 0x1000, test_buffer, 100);

	zassert_equal(result1, TT_BOOT_FS_ERR, "Should return error for NULL device");

	if (device_is_ready(FLASH_DEVICE)) {
		/* Test 2: NULL buffer */
		int result2 = tt_bootfs_ng_write(FLASH_DEVICE, 0x1000, NULL, 100);

		zassert_equal(result2, TT_BOOT_FS_ERR, "Should return error for NULL buffer");

		/* Test 3: Valid small write */
		memset(test_buffer, 0xAA, sizeof(test_buffer));
		int result3 = tt_bootfs_ng_write(FLASH_DEVICE, 0x1000, test_buffer, 4);

		zassert_true(result3 == TT_BOOT_FS_OK || result3 != TT_BOOT_FS_ERR,
			     "Should handle valid small write");
	}
}

ZTEST(tt_boot_fs, test_ng_erase_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	/* Test 1: NULL device */
	int result1 = tt_bootfs_ng_erase(NULL, 0x1000, 4096);

	zassert_equal(result1, TT_BOOT_FS_ERR, "Should return error for NULL device");

	if (device_is_ready(FLASH_DEVICE)) {
		/* Test 2: Zero size */
		int result2 = tt_bootfs_ng_erase(FLASH_DEVICE, 0x1000, 0);

		zassert_equal(result2, TT_BOOT_FS_ERR, "Should return error for zero size");

		/* Test 3: Valid erase */
		int result3 = tt_bootfs_ng_erase(FLASH_DEVICE, 0x2000, 4096);

		zassert_true(result3 == TT_BOOT_FS_OK || result3 != TT_BOOT_FS_ERR,
			     "Should handle valid erase operation");
	}
}

ZTEST(tt_boot_fs, test_bootfs_ls_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	if (!device_is_ready(FLASH_DEVICE)) {
		ztest_test_skip();
		return;
	}

	tt_boot_fs_fd fds[CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX];

	/* Test 1: NULL device */
	int result1 = tt_bootfs_ls(NULL, fds, CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX);

	zassert_equal(result1, TT_BOOT_FS_ERR, "Should return error for NULL device");

	/* Test 2: NULL fds array */
	int result2 = tt_bootfs_ls(FLASH_DEVICE, NULL, CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX);

	zassert_equal(result2, TT_BOOT_FS_ERR, "Should return error for NULL fds");

	/* Test 3: Zero nfds */
	int result3 = tt_bootfs_ls(FLASH_DEVICE, fds, 0);

	zassert_equal(result3, TT_BOOT_FS_ERR, "Should return error for zero nfds");

	/* Test 4: Single element array */
	tt_boot_fs_fd single_fd;

	int result4 = tt_bootfs_ls(FLASH_DEVICE, &single_fd, 1);

	zassert_true(result4 >= 0 || result4 != TT_BOOT_FS_ERR,
		     "Should handle single element array");
}

ZTEST_SUITE(tt_boot_fs, NULL, NULL, NULL, NULL, NULL);
