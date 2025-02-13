/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <tenstorrent/bitrev.h>

ZTEST(bitrev, test_bitrev4)
{
	zexpect_equal(bitrev4(0b0000), 0b0000);
	zexpect_equal(bitrev4(0b0001), 0b1000);
	zexpect_equal(bitrev4(0b0010), 0b0100);
	zexpect_equal(bitrev4(0b0011), 0b1100);
	zexpect_equal(bitrev4(0b0100), 0b0010);
	zexpect_equal(bitrev4(0b0101), 0b1010);
	zexpect_equal(bitrev4(0b0110), 0b0110);
	zexpect_equal(bitrev4(0b0111), 0b1110);
	zexpect_equal(bitrev4(0b1000), 0b0001);
	zexpect_equal(bitrev4(0b1001), 0b1001);
	zexpect_equal(bitrev4(0b1010), 0b0101);
	zexpect_equal(bitrev4(0b1011), 0b1101);
	zexpect_equal(bitrev4(0b1100), 0b0011);
	zexpect_equal(bitrev4(0b1101), 0b1011);
	zexpect_equal(bitrev4(0b1110), 0b0111);
	zexpect_equal(bitrev4(0b1111), 0b1111);
}

ZTEST(bitrev, test_bitrev8)
{
	zexpect_equal(bitrev8(0x00), 0x00);
	zexpect_equal(bitrev8(0x01), 0x80);
	zexpect_equal(bitrev8(0x5a), 0x5a);
	zexpect_equal(bitrev8(0xfe), 0x7f);
	zexpect_equal(bitrev8(0xff), 0xff);
}

ZTEST(bitrev, test_bitrev16)
{
	zexpect_equal(bitrev16(0x0000), 0x0000);
	zexpect_equal(bitrev16(0x0001), 0x8000);
	zexpect_equal(bitrev16(0x5a5a), 0x5a5a);
	zexpect_equal(bitrev16(0xfffe), 0x7fff);
	zexpect_equal(bitrev16(0xffff), 0xffff);
}

ZTEST(bitrev, test_bitrev32)
{
	zexpect_equal(bitrev32(0x00000000), 0x00000000);
	zexpect_equal(bitrev32(0x00000001), 0x80000000);
	zexpect_equal(bitrev32(0x5a5a5a5a), 0x5a5a5a5a);
	zexpect_equal(bitrev32(0xfffffffe), 0x7fffffff);
	zexpect_equal(bitrev32(0xffffffff), 0xffffffff);
}

ZTEST(bitrev, test_bitrev64)
{
	zexpect_equal(bitrev64(0x0000000000000000), 0x0000000000000000);
	zexpect_equal(bitrev64(0x0000000000000001), 0x8000000000000000);
	zexpect_equal(bitrev64(0x5a5a5a5a5a5a5a5a), 0x5a5a5a5a5a5a5a5a);
	zexpect_equal(bitrev64(0xfffffffffffffffe), 0x7fffffffffffffff);
	zexpect_equal(bitrev64(0xffffffffffffffff), 0xffffffffffffffff);
}

ZTEST_SUITE(bitrev, NULL, NULL, NULL, NULL, NULL);
