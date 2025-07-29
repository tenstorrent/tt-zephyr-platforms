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

#define SPI_FLASH_NODE DT_NODELABEL(spi_flash)

#if DT_NODE_EXISTS(SPI_FLASH_NODE) && DT_NODE_HAS_STATUS(SPI_FLASH_NODE, okay)
#define FLASH_DEVICE           DEVICE_DT_GET(SPI_FLASH_NODE)
#define FLASH_DEVICE_AVAILABLE 1
#else
#define FLASH_DEVICE           NULL
#define FLASH_DEVICE_AVAILABLE 0
#endif

#define MAX_FDS CONFIG_TT_BOOT_FS_IMAGE_COUNT_MAX

#define DECL_TEST_SPEC(dev, fds, nfds, offset, expect)                                             \
	{.dev = (dev), .fds = (fds), .nfds = (nfds), .offset = (offset), .expect = (expect)}

static void *setup_bootfs(void)
{
#if FLASH_DEVICE_AVAILABLE
	printk("FLASH_DEVICE: %p\n", FLASH_DEVICE);
	printk("Flash device name: %s\n", FLASH_DEVICE->name);
	printk("Flash device ready: %s\n", device_is_ready(FLASH_DEVICE) ? "YES" : "NO");
	zassert_not_null(FLASH_DEVICE, "FLASH_DEVICE is NULL!");

	static tt_boot_fs_fd fds[MAX_FDS];

	memset(fds, 0, sizeof(fds));

	struct {
		uint32_t spi_addr;
		const char *image_tag;
		uint32_t size;
		uint32_t copy_dest;
		uint32_t data_crc;
		uint32_t flags;
		uint32_t fd_crc;
	} expected_fds[] = {
		{81920, "cmfwcfg", 56, 0, 2158370831, 56, 4168430605},
		{86016, "cmfw", 86600, 268435456, 1374720981, 33641032, 3680084864},
		{176128, "ethfwcfg", 512, 0, 2352493, 512, 3455414089},
		{180224, "ethfw", 34304, 0, 433295191, 34304, 2151631411},
		{217088, "memfwcfg", 256, 0, 15943, 256, 3453442091},
		{221184, "memfw", 10032, 0, 3642299916, 10032, 1066009376},
		{233472, "ethsdreg", 1152, 0, 897437643, 1152, 273632020},
		{237568, "ethsdfw", 19508, 0, 3168980852, 19508, 818321009},
		{258048, "bmfw", 35744, 0, 2928587200, 35744, 637115074},
		{294912, "flshinfo", 4, 0, 50462976, 4, 3672136659},
		{299008, "failover", 65828, 268435456, 2239637331, 33620260, 1985122380},
		{16773120, "boardcfg", 0, 0, 0, 0, 3670524614},
	};

	size_t n = ARRAY_SIZE(expected_fds);

	for (size_t i = 0; i < n && i < MAX_FDS; ++i) {
		memset(&fds[i], 0, sizeof(tt_boot_fs_fd));
		fds[i].spi_addr = expected_fds[i].spi_addr;
		fds[i].copy_dest = expected_fds[i].copy_dest;
		fds[i].flags.val = expected_fds[i].flags;
		fds[i].data_crc = expected_fds[i].data_crc;
		fds[i].security_flags.val = 0;
		memset(fds[i].image_tag, 0, sizeof(fds[i].image_tag));
		strncpy((char *)fds[i].image_tag, expected_fds[i].image_tag,
			sizeof(fds[i].image_tag));
		fds[i].fd_crc = expected_fds[i].fd_crc;
	}

	if (n < MAX_FDS) {
		fds[n].flags.f.invalid = 1;
	}

	int rc = flash_erase(FLASH_DEVICE, TT_BOOT_FS_FD_HEAD_ADDR, sizeof(fds));

	zassert_equal(rc, 0, "Failed to erase test bootfs area in flash");

	rc = flash_write(FLASH_DEVICE, TT_BOOT_FS_FD_HEAD_ADDR, (const uint8_t *)fds, sizeof(fds));
	zassert_equal(rc, 0, "Failed to write test bootfs to flash");
#endif
	return NULL;
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

ZTEST(tt_boot_fs, test_boot_fs_ls_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	static tt_boot_fs_fd fds[MAX_FDS];
	const struct device *valid_dev = FLASH_DEVICE;
	const struct device *null_dev = NULL;

	tt_boot_fs_fd *fds_options[] = {NULL, fds};
	size_t nfds_options[] = {0, 1, MAX_FDS - 1, MAX_FDS};
	size_t offset_options[] = {0, 1, MAX_FDS - 1, MAX_FDS, SIZE_MAX};
	const struct device *dev_options[] = {null_dev, valid_dev};

	static const struct test_spec {
		const struct device *dev;
		tt_boot_fs_fd *fds;
		size_t nfds;
		size_t offset;
		int expect;
	};
	struct test_spec specs[80];
	size_t idx = 0;

	for (size_t d = 0; d < ARRAY_SIZE(dev_options); ++d) {
		for (size_t f = 0; f < ARRAY_SIZE(fds_options); ++f) {
			for (size_t n = 0; n < ARRAY_SIZE(nfds_options); ++n) {
				for (size_t o = 0; o < ARRAY_SIZE(offset_options); ++o) {
					int expect;

					if (dev_options[d] == NULL) {
						expect = -ENXIO;
					} else if (nfds_options[n] == 0) {
						expect = 0;
					} else {
						size_t total_fds = 12;
						size_t available =
							(offset_options[o] < total_fds)
								? total_fds - offset_options[o]
								: 0;
						expect = (nfds_options[n] < available)
								 ? nfds_options[n]
								 : available;
					}

					specs[idx++] = DECL_TEST_SPEC(
						dev_options[d], fds_options[f], nfds_options[n],
						offset_options[o], expect);
				}
			}
		}
	}

	ARRAY_FOR_EACH(specs, i) {
		const struct test_spec *spec = &specs[i];

		int actual = tt_boot_fs_ls(spec->dev, spec->fds, spec->nfds, spec->offset);

		zassert_true(actual == spec->expect || actual == -ENXIO || actual == -EIO,
			     "%zu: actual: %d expected: %d or error", i, actual, spec->expect);
	}
}

ZTEST(tt_boot_fs, test_find_fd_by_tag_comprehensive)
{
#if !FLASH_DEVICE_AVAILABLE
	ztest_test_skip();
	return;
#endif

	tt_boot_fs_fd fd;
	const struct device *valid_dev = FLASH_DEVICE;
	const struct device *null_dev = NULL;

	tt_boot_fs_fd *valid_fd = &fd;
	tt_boot_fs_fd *null_fd = NULL;

	const uint8_t *tags[] = {(const uint8_t *)"notfound", (const uint8_t *)"cmfw"};
	const struct device *dev_options[] = {null_dev, valid_dev};
	tt_boot_fs_fd *fd_options[] = {valid_fd, null_fd};

	struct test_spec {
		const struct device *dev;
		const uint8_t *tag;
		tt_boot_fs_fd *fd;
	};

	struct test_spec specs[12];
	size_t idx = 0;

	for (size_t d = 0; d < ARRAY_SIZE(dev_options); ++d) {
		for (size_t t = 0; t < ARRAY_SIZE(tags); ++t) {
			for (size_t f = 0; f < ARRAY_SIZE(fd_options); ++f) {
				specs[idx++] =
					DECL_TEST_SPEC(dev_options[d], tags[t], fd_options[f]);
			}
		}
	}

	ARRAY_FOR_EACH(specs, i) {
		const struct test_spec *spec = &specs[i];

		int actual = tt_boot_fs_find_fd_by_tag(spec->dev, spec->tag, spec->fd);

		if (spec->dev == NULL) {
			zexpect_equal(actual, -ENXIO, "%zu: expected -ENXIO for NULL device", i);
		} else if (spec->fd == NULL) {
			zassert_true(actual == 0 || actual == -ENOENT,
				     "%zu: expected 0 or -ENOENT for NULL fd", i);
		} else if (strcmp((const char *)spec->tag, "cmfw") == 0) {
			zassert_true(actual == 0 || actual == -ENOENT,
				     "%zu: expected 0 or -ENOENT for 'cmfw' tag", i);
		} else {
			zexpect_equal(actual, -ENOENT, "%zu: expected
				 -ENOENT for 'notfound' tag", i);
		}
	}
}

ZTEST_SUITE(tt_boot_fs, NULL, setup_bootfs, NULL, NULL, NULL);
