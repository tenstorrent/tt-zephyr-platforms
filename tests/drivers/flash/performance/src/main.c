/*
 * Copyright (c) 2020-2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/irq.h>

#define TEST_AREA	storage_partition

#define TEST_AREA_OFFSET	FIXED_PARTITION_OFFSET(TEST_AREA)
#define TEST_AREA_SIZE		FIXED_PARTITION_SIZE(TEST_AREA)
#define TEST_AREA_MAX		(TEST_AREA_OFFSET + TEST_AREA_SIZE)
#define TEST_AREA_DEVICE	FIXED_PARTITION_DEVICE(TEST_AREA)

#define EXPECTED_SIZE	MIN(TEST_AREA_SIZE, 0x100000)

static const struct device *const flash_dev = TEST_AREA_DEVICE;
static uint8_t buf[EXPECTED_SIZE];
static uint8_t check_buf[EXPECTED_SIZE];

static int flash_program_wrap(const struct device *dev, off_t offset,
			const void *data, size_t len)
{
	int rc = flash_erase(dev, offset, len);

	if (rc != 0) {
		return rc;
	}
	return flash_write(dev, offset, data, len);
}

ZTEST(flash_driver_perf, test_read_perf)
{
	int rc;
	int64_t ts = k_uptime_get();
	int64_t delta;

	rc = flash_read(flash_dev, TEST_AREA_OFFSET, buf, EXPECTED_SIZE);
	delta = k_uptime_delta(&ts);
	zassert_equal(rc, 0, "Cannot read flash");
	TC_PRINT("Read performance test ran in %lld ms\n", delta);
	zassert_true(delta < CONFIG_EXPECTED_READ_TIME, "Read performance test failed");
}

ZTEST(flash_driver_perf, test_program_perf)
{
	int rc;
	int64_t ts = k_uptime_get();
	int64_t delta;

	/* Create data to write to erased region */
	for (int i = 0; i < EXPECTED_SIZE; i++) {
		buf[i] = (uint8_t)(i & 0xff);
	}

	/* Write buffer to flash */
	ts = k_uptime_get();
	rc = flash_program_wrap(flash_dev, TEST_AREA_OFFSET, buf, EXPECTED_SIZE);
	delta = k_uptime_delta(&ts);
	zassert_equal(rc, 0, "Cannot program flash");
	TC_PRINT("Program performance test ran in %lld ms\n", delta);
	zassert_true(delta < CONFIG_EXPECTED_PROGRAM_TIME, "Program performance test failed");
	/* Read back the data */
	rc = flash_read(flash_dev, TEST_AREA_OFFSET, check_buf, EXPECTED_SIZE);
	zassert_equal(rc, 0, "Cannot read flash");
	/* Check that the data read back is the same as the data written */
	zassert_mem_equal(buf, check_buf, EXPECTED_SIZE,
			"Data read back from flash does not match data written");
	TC_PRINT("Data read back from flash matches data written\n");
}


ZTEST_SUITE(flash_driver_perf, NULL, NULL, NULL, NULL, NULL);
