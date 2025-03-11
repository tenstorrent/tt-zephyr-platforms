/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#define MAX_WRITE KB(32)
#define TEST_AREA_OFFSET FIXED_PARTITION_OFFSET(storage_partition)
#define TEST_AREA_SIZE MIN(FIXED_PARTITION_SIZE(storage_partition), MAX_WRITE)

const struct device *flash_dev = FIXED_PARTITION_DEVICE(storage_partition);

uint8_t buf[TEST_AREA_SIZE];
uint8_t check_buf[TEST_AREA_SIZE];

int main(void)
{
	struct flash_pages_info page_info;
	int rc;
	int64_t uptime;

	flash_get_page_info_by_offs(flash_dev, TEST_AREA_OFFSET, &page_info);
	printf("Erasing %d pages at 0x%lx\n", (sizeof(buf) / page_info.size),
	       page_info.start_offset);
	uptime = k_uptime_get();
	/* Erase flash blocks */
	rc = flash_erase(flash_dev, page_info.start_offset, sizeof(buf));
	if (rc < 0) {
		printf("Erase failed: %d\n", rc);
		return rc;
	}
	printf("Erase took %lld ms\n", k_uptime_delta(&uptime));
	for (uint32_t i = 0; i < sizeof(buf); i++) {
		buf[i] = i & 0xff;
	}
	memcpy(check_buf, buf, sizeof(buf));
	/* Write flash blocks */
	uptime = k_uptime_get();
	rc = flash_write(flash_dev, page_info.start_offset, buf, sizeof(buf));
	if (rc < 0) {
		printf("Write failed: %d\n", rc);
		return rc;
	}
	printf("Write of %d bytes took %lld ms\n", sizeof(buf),
	       k_uptime_delta(&uptime));
	/* Read back flash blocks */
	rc = flash_read(flash_dev, page_info.start_offset, buf, sizeof(buf));
	uptime = k_uptime_delta(&uptime);
	if (memcmp(check_buf, buf, sizeof(buf))) {
		printf("Read back failed\n");
		return -EIO;
	}
	printf("Read of %d bytes took %lld ms\n", sizeof(buf), uptime);
	printf("Flash performance test complete\n");
	return 0;
}
