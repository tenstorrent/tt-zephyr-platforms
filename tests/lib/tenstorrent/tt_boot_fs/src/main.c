/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <tenstorrent/tt_boot_fs.h>

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

ZTEST_SUITE(tt_boot_fs, NULL, NULL, NULL, NULL, NULL);
