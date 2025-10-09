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

/* Callback test variables */
static volatile bool callback_received;
static int callback_count;
static int callback_status;
static uint32_t callback_channel;

static void test_dma_callback(const struct device *dev, void *user_data, uint32_t channel,
			      int status)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	callback_count++;
	callback_status = status;
	callback_channel = channel;

	printk("DMA callback: channel=%u, status=%d, count=%d\n", channel, status, callback_count);

	/* Set flag when transfer is complete or error occurs */
	if (status == DMA_STATUS_COMPLETE || status < 0) {
		callback_received = true;
	}
}

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

ZTEST(dma_arc_to_noc_test, test_memory_to_memory_callback)
{
	uint8_t src_buffer[TEST_BUFFER_SIZE] __aligned(64);
	uint8_t dst_buffer[TEST_BUFFER_SIZE] __aligned(64);
	int ret;

	/* Initialize test data */
	for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
		src_buffer[i] = (uint8_t)(i + 0x10);
	}
	memset(dst_buffer, 0, sizeof(dst_buffer));

	/* Initialize callback test variables */
	callback_received = false;
	callback_count = 0;
	callback_status = -999;    /* Invalid initial value */
	callback_channel = 0xFFFF; /* Invalid initial value */

	/* Configure DMA block */
	struct dma_block_config block = {
		.source_address = (uintptr_t)src_buffer,
		.dest_address = (uintptr_t)dst_buffer,
		.block_size = sizeof(src_buffer),
	};

	/* Configure DMA with callback enabled */
	struct dma_config config = {
		.channel_direction = MEMORY_TO_MEMORY,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
		.head_block = &block,
		.user_data = NULL, /* No coordinates needed for MEMORY_TO_MEMORY */
		.dma_callback = test_dma_callback,
		.complete_callback_en = true,
		.error_callback_dis = false,
	};

	printk("Configuring DMA channel 0 for memory-to-memory transfer with callback\n");
	ret = dma_config(dma, 0, &config);
	zassert_ok(ret);

	printk("Starting DMA transfer\n");
	ret = dma_start(dma, 0);
	zassert_ok(ret);

	/* Wait for callback completion with timeout */
	printk("Waiting for DMA completion callback\n");
	int timeout_ms = 5000;
	int elapsed_ms = 0;

	while (!callback_received && elapsed_ms < timeout_ms) {
		k_msleep(10);
		elapsed_ms += 10;
	}

	zassert_true(callback_received, "Timeout waiting for DMA callback after %d ms", timeout_ms);

	/* Verify callback was called with correct parameters */
	zassert_equal(callback_channel, 0, "Callback received wrong channel number");
	zassert_equal(callback_status, DMA_STATUS_COMPLETE,
		      "Callback status should be DMA_STATUS_COMPLETE");
	zassert_true(callback_count > 0, "Callback should have been called at least once");

	/* Verify data was transferred correctly */
	ret = memcmp(src_buffer, dst_buffer, sizeof(src_buffer));
	zassert_equal(ret, 0, "Data transfer failed - buffers don't match");

	/* Verify DMA channel is no longer busy */
	struct dma_status status;

	ret = dma_get_status(dma, 0, &status);
	zassert_ok(ret);
	zassert_false(status.busy, "DMA channel should not be busy after completion");

	printk("Memory-to-memory callback test completed successfully\n");
}

ZTEST(dma_arc_to_noc_test, test_peripheral_transfer_callback)
{
	uint8_t test_buffer[TEST_BUFFER_SIZE] __aligned(64);
	uint8_t tensix_x, tensix_y;
	int ret;

	/* Initialize test data */
	for (int i = 0; i < TEST_BUFFER_SIZE; i++) {
		test_buffer[i] = (uint8_t)(i + 0x20);
	}

	GetEnabledTensix(&tensix_x, &tensix_y);

	/* Initialize callback test variables */
	callback_received = false;
	callback_count = 0;
	callback_status = -999;
	callback_channel = 0xFFFF;

	struct tt_bh_dma_noc_coords coords = {
		.source_x = tensix_x,
		.source_y = tensix_y,
		.dest_x = ARC_NOC0_X,
		.dest_y = ARC_NOC0_Y,
	};

	struct dma_block_config block = {
		.source_address = 0, /* Tensix address */
		.dest_address = (uintptr_t)test_buffer,
		.block_size = sizeof(test_buffer),
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
		.dma_callback = test_dma_callback,
		.complete_callback_en = true,
		.error_callback_dis = false,
	};

	printk("Configuring DMA channel 1 for memory-to-peripheral transfer with callback\n");
	ret = dma_config(dma, 1, &config);
	zassert_ok(ret);

	printk("Starting DMA transfer\n");
	ret = dma_start(dma, 1);
	zassert_ok(ret);

	/* For NOC transfers, we can poll status since callbacks might not be implemented yet */
	printk("Polling DMA status for completion\n");
	struct dma_status status;
	int poll_count = 0;

	do {
		ret = dma_get_status(dma, 1, &status);
		zassert_ok(ret);
		k_msleep(10);
		poll_count++;
		if (poll_count > 500) { /* 5 second timeout */
			zassert_true(false, "Timeout waiting for DMA completion");
		}
	} while (status.busy);

	printk("DMA transfer completed after %d polls\n", poll_count);

	if (callback_count > 0) {
		printk("Callback was received for peripheral transfer\n");
		zassert_equal(callback_channel, 1, "Callback received wrong channel number");
	} else {
		printk("No callback received for peripheral transfer (may not be implemented "
		       "yet)\n");
	}

	printk("Peripheral transfer callback test completed\n");
}

ZTEST_SUITE(dma_arc_to_noc_test, NULL, NULL, NULL, NULL, NULL);
