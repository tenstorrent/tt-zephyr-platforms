/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_tt_bh_noc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <string.h>

#include "noc_init.h"

#define ARC_NOC0_X       8
#define ARC_NOC0_Y       0
#define TEST_BUFFER_SIZE 100

const struct device *dma = DEVICE_DT_GET(DT_NODELABEL(dma1));

ZTEST(dma_arc_to_noc_test, test_write_read)
{
	uint8_t write_buffer[TEST_BUFFER_SIZE] __aligned(64);
	uint8_t read_buffer[TEST_BUFFER_SIZE] __aligned(64);
	uint8_t tensix_x, tensix_y;
	int ret;

	for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
		write_buffer[i] = (uint8_t)(i & 0xFF);
	}

	memset(read_buffer, 0, sizeof(read_buffer));

	GetEnabledTensix(&tensix_x, &tensix_y);

	struct tt_bh_dma_noc_coords coords = {
		.source_x = tensix_x,
		.source_y = tensix_y,
		.dest_x = ARC_NOC0_X,
		.dest_y = ARC_NOC0_Y,
	};

	struct dma_block_config block = {
		.source_address = 0,
		.dest_address = (uintptr_t)write_buffer,
		.block_size = sizeof(write_buffer),
	};

	struct dma_config config = {
		.channel_direction = MEMORY_TO_PERIPHERAL,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
		.head_block = &block,
		.user_data = &coords,
	};

	ret = dma_config(dma, 1, &config);
	zassert_ok(ret);

	ret = dma_start(dma, 1);
	zassert_ok(ret);

	block = (struct dma_block_config){
		.source_address = 0,
		.dest_address = (uintptr_t)read_buffer,
		.block_size = sizeof(read_buffer),
	};

	config.channel_direction = PERIPHERAL_TO_MEMORY;

	ret = dma_config(dma, 1, &config);
	zassert_ok(ret);

	ret = dma_start(dma, 1);
	zassert_ok(ret);

	ret = memcmp(write_buffer, read_buffer, sizeof(write_buffer));
	zassert_equal(ret, 0);
}

ZTEST_SUITE(dma_arc_to_noc_test, NULL, NULL, NULL, NULL, NULL);
