/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_noc_dma

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_tt_bh_noc.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/kernel.h>
#include <string.h>

#include "noc.h"
#include "noc2axi.h"
#include "util.h"

LOG_MODULE_REGISTER(dma_noc_tt_bh, CONFIG_DMA_LOG_LEVEL);

#define NOC_DMA_TLB        0
#define NOC_DMA_NOC_ID     0
#define NOC_DMA_TIMEOUT_MS 100
#define NOC_MAX_BURST_SIZE 16384

#define DMA_MAX_TRANSFER_BLOCKS 8

/* NOC CMD fields */
#define NOC_CMD_CPY               (0 << 0)
#define NOC_CMD_RD                (0 << 1)
#define NOC_CMD_WR                (1 << 1)
#define NOC_CMD_RESP_MARKED       (1 << 4)
#define NOC_CMD_BRCST_PACKET      (1 << 5)
#define NOC_CMD_PATH_RESERVE      (1 << 8)
#define NOC_CMD_BRCST_SRC_INCLUDE (1 << 17)

/* NOC0 RISC0 DMA registers */
#define TARGET_ADDR_LO           0xFFB20000
#define TARGET_ADDR_MID          0xFFB20004
#define TARGET_ADDR_HI           0xFFB20008
#define RET_ADDR_LO              0xFFB2000C
#define RET_ADDR_MID             0xFFB20010
#define RET_ADDR_HI              0xFFB20014
#define PACKET_TAG               0xFFB20018
#define CMD_BRCST                0xFFB2001C
#define AT_LEN                   0xFFB20020
#define AT_LEN_1                 0xFFB20024
#define AT_DATA                  0xFFB20028
#define BRCST_EXCLUDE            0xFFB2002C
#define CMD_CTRL                 0xFFB20040
#define NIU_MST_WR_ACK_RECEIVED  0xFFB20204
#define NIU_MST_RD_RESP_RECEIVED 0xFFB20208

/* Define invalid channel constant - using a high value that's unlikely to be used */
#define DMA_CHANNEL_INVALID 0xFFFFFFFF

/* Context structure for async work - define before struct that uses it */
struct noc_dma_work_context {
	struct k_work_delayable work;
	const struct device *dev;
	uint32_t channel;
};

static int tt_bh_dma_noc_start(const struct device *dev, uint32_t channel);

struct tt_bh_dma_channel_data {
	struct dma_block_config blocks[DMA_MAX_TRANSFER_BLOCKS];
	int block_index;
	int block_count;
	struct tt_bh_dma_noc_coords coords;
	struct dma_config config;
	struct noc_dma_work_context work_ctx;
	bool active;
	bool configured;
	bool suspended;
	bool cyclic_active;
};

/*
 * Initialized in the TT_BH_DMA_NOC_INIT() macro.
 */
struct tt_bh_dma_noc_config {
	/* Initialized to a static array of channels. */
	struct tt_bh_dma_channel_data *channels;

	/* Initialized to the dma-channels devicetree property. */
	uint8_t num_channels;
};

/*
 * Initialized in the tt_bh_dma_noc_init() functions.
 */
struct tt_bh_dma_noc_data {
	struct k_mutex lock;
};

struct ret_addr_hi {
	uint32_t end_x: 6;
	uint32_t end_y: 6;
	uint32_t start_x: 6;
	uint32_t start_y: 6;
};

union ret_addr_hi_u {
	struct ret_addr_hi f;
	uint32_t u;
};

static bool noc_wait_cmd_ready(void)
{
#ifdef CONFIG_BOARD_NATIVE_SIM
	/* Fake completion */
	return true;
#else
	uint32_t cmd_ctrl;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(NOC_DMA_TIMEOUT_MS));

	do {
		cmd_ctrl = NOC2AXIRead32(NOC_DMA_NOC_ID, NOC_DMA_TLB, CMD_CTRL);
	} while (cmd_ctrl != 0 && !sys_timepoint_expired(timeout));

	return cmd_ctrl == 0;
#endif
}

static uint32_t get_expected_acks(uint32_t noc_cmd, uint64_t size)
{
	uint32_t ack_reg_addr =
		(noc_cmd & NOC_CMD_WR) ? NIU_MST_WR_ACK_RECEIVED : NIU_MST_RD_RESP_RECEIVED;
	uint32_t packet_received = NOC2AXIRead32(NOC_DMA_NOC_ID, NOC_DMA_TLB, ack_reg_addr);
	uint32_t expected_acks = packet_received + DIV_ROUND_UP(size, NOC_MAX_BURST_SIZE);

	return expected_acks;
}

/* wrap around aware comparison for half-range rule */
static inline bool is_behind(uint32_t current, uint32_t target)
{
	/*
	 * "target" and "current" are NOC transaction counters, and may wrap around, so we must
	 * consider the case where target and current have wrapped a different number of times.
	 * There's no way to know how many times they have wrapped, instead we assume that they are
	 * within 2**31 of each other as that gives an unambiguous ordering.
	 *
	 * We deal with this by considering
	 * target - 2**31 <  current < target         MOD 2**32 as before target and
	 * target         <= current < target + 2**31 MOD 2**32 as after target.
	 *
	 * We can't just check target == current because just one spurious NOC transaction could
	 * result in the loop hanging with current = target+1.
	 */
	return (int32_t)(current - target) < 0;
}

static bool wait_noc_dma_done(uint32_t noc_cmd, uint32_t expected_acks)
{
#ifdef CONFIG_BOARD_NATIVE_SIM
	/* Fake NOC completion */
	return true;
#else
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(NOC_DMA_TIMEOUT_MS));
	uint32_t ack_reg_addr =
		(noc_cmd & NOC_CMD_WR) ? NIU_MST_WR_ACK_RECEIVED : NIU_MST_RD_RESP_RECEIVED;
	uint32_t ack_received;
	bool behind;

	do {
		ack_received = NOC2AXIRead32(NOC_DMA_NOC_ID, NOC_DMA_TLB, ack_reg_addr);
		behind = is_behind(ack_received, expected_acks);
	} while (behind && !sys_timepoint_expired(timeout));

	return !behind;
#endif
}

static uint32_t noc_dma_format_coord(uint8_t x, uint8_t y)
{
	/* clang-format off */
	return (union ret_addr_hi_u){
		.f = { .end_x = x, .end_y = y }
	} .u;
	/* clang-format on */
}

static int noc_dma_transfer(uint32_t cmd, uint32_t ret_coord, uint64_t ret_addr,
			    uint32_t targ_coord, uint64_t targ_addr, uint32_t size, bool multicast,
			    uint8_t transaction_id, bool include_self)
{
	uint32_t ret_addr_lo = low32(ret_addr);
	uint32_t ret_addr_mid = high32(ret_addr);
	uint32_t ret_addr_hi = ret_coord;

	uint32_t targ_addr_lo = low32(targ_addr);
	uint32_t targ_addr_mid = high32(targ_addr);
	uint32_t targ_addr_hi = targ_coord;

	uint32_t noc_at_len_be = size;
	uint32_t noc_packet_tag = transaction_id << 10;

	uint32_t noc_ctrl = NOC_CMD_CPY | cmd;

	if (multicast) {
		noc_ctrl |= NOC_CMD_PATH_RESERVE | NOC_CMD_BRCST_PACKET;

		if (include_self) {
			noc_ctrl |= NOC_CMD_BRCST_SRC_INCLUDE;
		}
	}

	if (!noc_wait_cmd_ready()) {
		return 1;
	}

	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, TARGET_ADDR_LO, targ_addr_lo);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, TARGET_ADDR_MID, targ_addr_mid);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, TARGET_ADDR_HI, targ_addr_hi);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, RET_ADDR_LO, ret_addr_lo);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, RET_ADDR_MID, ret_addr_mid);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, RET_ADDR_HI, ret_addr_hi);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, PACKET_TAG, noc_packet_tag);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, AT_LEN, noc_at_len_be);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, AT_LEN_1, 0);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, AT_DATA, 0);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, BRCST_EXCLUDE, 0);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, CMD_BRCST, noc_ctrl);
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, CMD_CTRL, 1);

	return 0;
}

/*
 * Async work handler for memory-to-memory transfers (ARC to ARC within TLB window)
 *
 * Callback specifications for async transfers:
 * - Per-block callbacks (DMA_STATUS_BLOCK) when complete_callback_en is set
 * - Transfer completion callbacks (DMA_STATUS_COMPLETE) when all blocks are done
 * - Error callbacks (negative errno) when error_callback_dis is not set
 */
static void noc_dma_memcpy_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct noc_dma_work_context *ctx = CONTAINER_OF(dwork, struct noc_dma_work_context, work);
	const struct tt_bh_dma_noc_config *cfg =
		(const struct tt_bh_dma_noc_config *)ctx->dev->config;
	struct tt_bh_dma_channel_data *chan_data = &cfg->channels[ctx->channel];

	__ASSERT_NO_MSG(chan_data->config.channel_direction == MEMORY_TO_MEMORY);

	/* Check if transfer is suspended */
	if (chan_data->suspended) {
		/* Re-schedule to check again later */
		k_work_reschedule(&chan_data->work_ctx.work, K_MSEC(1));
		return;
	}

	/* Get current block to process */
	struct dma_block_config *current_block = &chan_data->blocks[chan_data->block_index];

	if (!current_block || chan_data->block_index >= chan_data->block_count) {
		LOG_ERR("Invalid block index %d", chan_data->block_index);
		chan_data->active = false;

		/* Call error callback using helper function */
		handle_transfer_callbacks(ctx->dev, chan_data, ctx->channel, -EINVAL, true);
		return;
	}

	/* Local memory copy for current block */
	memcpy((void *)(uintptr_t)current_block->dest_address,
	       (void *)(uintptr_t)current_block->source_address, current_block->block_size);

	chan_data->block_index++;

	/* Check if we have more blocks to process */
	bool more_blocks = (chan_data->block_index < chan_data->block_count);

	/* Handle callbacks using the unified helper function */
	handle_transfer_callbacks(ctx->dev, chan_data, ctx->channel, 0, !more_blocks);

	/* If we have more blocks to process, continue with next block */
	if (more_blocks) {
		k_work_reschedule(&chan_data->work_ctx.work, K_NO_WAIT);
		return;
	}

	/* Handle cyclic transfers */
	if (chan_data->config.cyclic && chan_data->cyclic_active) {
		/* For cyclic transfers, reset to first block and reschedule */
		chan_data->block_index = 0;
		k_work_reschedule(&chan_data->work_ctx.work, K_MSEC(1));
		return;
	}

	/* Handle channel linking for non-cyclic transfers */
	if (chan_data->config.linked_channel != DMA_CHANNEL_INVALID) {
		uint32_t linked_chan = chan_data->config.linked_channel;

		if (linked_chan < cfg->num_channels && cfg->channels[linked_chan].configured) {

			/* Trigger linked channel based on chaining configuration */
			bool trigger_linked = false;

			if (chan_data->config.dest_chaining_en) {
				/* Major loop link - trigger at end of all blocks */
				trigger_linked = true;
			}
			if (chan_data->config.source_chaining_en) {
				/* Minor loop link - trigger during transfer */
				trigger_linked = true;
			}

			if (trigger_linked) {
				LOG_DBG("Triggering linked channel %u from channel %u", linked_chan,
					ctx->channel);
				tt_bh_dma_noc_start(ctx->dev, linked_chan);
			}
		}
	}

	/* Mark as inactive if not cyclic */
	if (!chan_data->config.cyclic || !chan_data->cyclic_active) {
		chan_data->active = false;
	}
}

static struct tt_bh_dma_channel_data *get_channel_data(const struct device *dev, uint32_t channel)
{
	const struct tt_bh_dma_noc_config *cfg = (const struct tt_bh_dma_noc_config *)dev->config;

	if (channel >= cfg->num_channels) {
		return NULL;
	}

	return &cfg->channels[channel];
}

/**
 * @brief Handle DMA transfer completion callbacks
 * 
 * This function handles callbacks for both synchronous and asynchronous transfers
 * based on the configuration.
 * 
 * @param dev DMA device
 * @param chan_data Channel data structure  
 * @param channel Channel number
 * @param transfer_ret Return value from transfer operation (0 = success)
 * @param is_final_block True if this is the final block in the transfer
 */
static void handle_transfer_callbacks(const struct device *dev,
				     struct tt_bh_dma_channel_data *chan_data,
				     uint32_t channel, int transfer_ret, bool is_final_block)
{
	if (!chan_data->config.dma_callback) {
		return;
	}

	if (transfer_ret == 0) {
		/* Success callbacks */
		if (chan_data->config.complete_callback_en && !is_final_block) {
			/* Per-block callback */
			chan_data->config.dma_callback(dev, chan_data->config.user_data,
						      channel, DMA_STATUS_BLOCK);
		}
		
		if (is_final_block) {
			/* Transfer completion callback */
			chan_data->config.dma_callback(dev, chan_data->config.user_data,
						      channel, DMA_STATUS_COMPLETE);
		}
	} else if (!chan_data->config.error_callback_dis) {
		/* Error callback - pass negative errno */
		chan_data->config.dma_callback(dev, chan_data->config.user_data,
					      channel, -EIO);
	}
}

/*
 * Config the source and dest NOC coordinates, the source and dest addresses and
 * the size of data transfer.
 *
 * Transfer data using only one block for simplicity.
 */
static int tt_bh_dma_noc_config(const struct device *dev, uint32_t channel,
				struct dma_config *config)
{
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);
	struct tt_bh_dma_noc_coords *coords = NULL;

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!config->head_block) {
		LOG_ERR("No block configuration provided");
		return -EINVAL;
	}

	/* user_data contains coordinates for non-memory-to-memory transfers */
	if (config->user_data) {
		coords = (struct tt_bh_dma_noc_coords *)config->user_data;
	} else {
		/* No coordinates - only valid for MEMORY_TO_MEMORY */
		if (config->channel_direction != MEMORY_TO_MEMORY) {
			LOG_ERR("Coordinates required for non-memory-to-memory transfers");
			return -EINVAL;
		}
	}

	/* Validate configuration */
	if (config->block_count == 0) {
		LOG_ERR("No block configuration");
		return -EINVAL;
	}

	if (config->block_count > DMA_MAX_TRANSFER_BLOCKS) {
		LOG_ERR("Too many blocks: %u > %u", config->block_count, DMA_MAX_TRANSFER_BLOCKS);
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Deep copy all blocks from the linked list */
	struct dma_block_config *src_block = config->head_block;

	chan_data->block_count = 0;

	for (int i = 0; i < config->block_count && src_block != NULL; i++) {
		/* Copy the block data */
		chan_data->blocks[i] = *src_block;
		/* Clear the next_block pointer since we're storing in an array */
		chan_data->blocks[i].next_block = NULL;
		chan_data->block_count++;

		/* Move to next block in the linked list */
		src_block = src_block->next_block;
	}

	chan_data->block_index = 0;
	chan_data->config = *config;
	/* Update the config to point to our copied blocks */
	chan_data->config.head_block = &chan_data->blocks[0];
	chan_data->configured = true;
	chan_data->active = false;

	/* Handle coordinates - for memory-to-memory, coords may be NULL */
	if (coords) {
		chan_data->coords = *coords;
	} else {
		/* Default to same coordinates for memory-to-memory */
		chan_data->coords.source_x = 0;
		chan_data->coords.source_y = 0;
		chan_data->coords.dest_x = 0;
		chan_data->coords.dest_y = 0;
	}

	k_mutex_unlock(&data->lock);

	return 0;
}

static uint32_t noc_dma_format_multicast(uint8_t start_x, uint8_t start_y, uint8_t end_x,
					 uint8_t end_y)
{
	return (union ret_addr_hi_u){
		.f = {.end_x = end_x, .end_y = end_y, .start_x = start_x, .start_y = start_y}}
		.u;
}

static int noc_dma_write_multicast(uint8_t local_x, uint8_t local_y, uint64_t local_addr,
				   uint8_t remote_start_x, uint8_t remote_start_y,
				   uint8_t remote_end_x, uint8_t remote_end_y, uint64_t remote_addr,
				   uint32_t size, bool include_self)
{
	uint32_t ret_coord = noc_dma_format_multicast(remote_start_x, remote_start_y, remote_end_x,
						      remote_end_y);
	uint64_t ret_addr = remote_addr;

	uint32_t targ_coord = noc_dma_format_coord(local_x, local_y);
	uint64_t targ_addr = local_addr;

	NOC2AXITlbSetup(NOC_DMA_NOC_ID, NOC_DMA_TLB, local_x, local_y, TARGET_ADDR_LO);

	return noc_dma_transfer(NOC_CMD_WR, ret_coord, ret_addr, targ_coord, targ_addr, size, true,
				0, include_self);
}

static int tt_bh_dma_noc_start(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!chan_data->configured) {
		LOG_ERR("Channel %u not configured", channel);
		return -EINVAL;
	}

	if (chan_data->active) {
		LOG_ERR("Channel %u already active", channel);
		return -EBUSY;
	}

	chan_data->active = true;
	chan_data->suspended = false;
	chan_data->block_index = 0;

	/* Enable cyclic mode if configured */
	if (chan_data->config.cyclic) {
		chan_data->cyclic_active = true;
	}

	struct tt_bh_dma_noc_coords *coords = &chan_data->coords;
	struct dma_block_config *current_block = &chan_data->blocks[chan_data->block_index];

	if (!current_block) {
		LOG_ERR("No valid block configuration");
		chan_data->active = false;
		return -EINVAL;
	}

	/* Handle different transfer types - all asynchronous */
	switch (chan_data->config.channel_direction) {
	case MEMORY_TO_MEMORY: {
		/* Memory-to-memory transfers use async work queue */
		k_work_reschedule(&chan_data->work_ctx.work, K_NO_WAIT);
		return 0;
	}
	case MEMORY_TO_PERIPHERAL: {
		uint32_t ret_coord = noc_dma_format_coord(coords->source_x, coords->source_y);
		uint64_t ret_addr = current_block->source_address;

		uint32_t targ_coord = noc_dma_format_coord(coords->dest_x, coords->dest_y);
		uint64_t targ_addr = current_block->dest_address;

		NOC2AXITlbSetup(NOC_DMA_NOC_ID, NOC_DMA_TLB, coords->source_x, coords->source_y,
				TARGET_ADDR_LO);

		int ret = noc_dma_transfer(NOC_CMD_RD, ret_coord, ret_addr, targ_coord, targ_addr,
					   current_block->block_size, false, 0, false);

		/* For async mode, callbacks will be handled when transfer actually completes */
		if (ret != 0) {
			/* Immediate error */
			handle_transfer_callbacks(dev, chan_data, channel, ret, true);
			chan_data->active = false;
		}
		return ret;
	}
	case PERIPHERAL_TO_MEMORY: {
		uint32_t ret_coord = noc_dma_format_coord(coords->dest_x, coords->dest_y);
		uint64_t ret_addr = current_block->dest_address;

		uint32_t targ_coord = noc_dma_format_coord(coords->source_x, coords->source_y);
		uint64_t targ_addr = current_block->source_address;

		NOC2AXITlbSetup(NOC_DMA_NOC_ID, NOC_DMA_TLB, coords->source_x, coords->source_y,
				TARGET_ADDR_LO);

		int ret = noc_dma_transfer(NOC_CMD_WR, ret_coord, ret_addr, targ_coord, targ_addr,
					   current_block->block_size, false, 0, false);

		/* For async mode, callbacks will be handled when transfer actually completes */
		if (ret != 0) {
			/* Immediate error */
			handle_transfer_callbacks(dev, chan_data, channel, ret, true);
			chan_data->active = false;
		}
		return ret;
	}
	case TT_BH_DMA_NOC_CHANNEL_DIRECTION_BROADCAST: {
		/* Use pre translation coords as NOC translation has enabled. */
		uint8_t remote_start_x = 2;
		uint8_t remote_start_y = 2;
		uint8_t remote_end_x = 1;
		uint8_t remote_end_y = 11;
		uint64_t local_addr = current_block->source_address;
		uint64_t remote_addr = current_block->dest_address;

		int ret = noc_dma_write_multicast(
			coords->dest_x, coords->dest_y, local_addr, remote_start_x, remote_start_y,
			remote_end_x, remote_end_y, remote_addr, current_block->block_size, false);

		/* For async mode, callbacks will be handled when transfer actually completes */
		if (ret != 0) {
			/* Immediate error */
			handle_transfer_callbacks(dev, chan_data, channel, ret, true);
			chan_data->active = false;
		}
		return ret;
	}
	default:
		LOG_ERR("%s: Invalid channel direction %d", __func__,
			chan_data->config.channel_direction);
		return -EINVAL;
	}
}

static int tt_bh_dma_noc_stop(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	/* Cancel any pending async work for this channel */
	k_work_cancel_delayable(&chan_data->work_ctx.work);

	/* Stop cyclic transfers */
	chan_data->cyclic_active = false;
	chan_data->suspended = false;
	chan_data->active = false;
	chan_data->block_index = 0;

	return 0;
}

static int tt_bh_dma_noc_suspend(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!chan_data->active) {
		LOG_ERR("Channel %u not active", channel);
		return -EINVAL;
	}

	chan_data->suspended = true;
	LOG_DBG("Suspended channel %u", channel);

	return 0;
}

static int tt_bh_dma_noc_resume(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!chan_data->active) {
		LOG_ERR("Channel %u not active", channel);
		return -EINVAL;
	}

	chan_data->suspended = false;
	LOG_DBG("Resumed channel %u", channel);

	/* If cyclic and suspended, reschedule work */
	if (chan_data->config.cyclic && chan_data->cyclic_active) {
		k_work_reschedule(&chan_data->work_ctx.work, K_NO_WAIT);
	}

	return 0;
}

static void tt_bh_dma_noc_release_channel(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Stop any ongoing transfer */
	if (chan_data->active) {
		tt_bh_dma_noc_stop(dev, channel);
	}

	/* Reset channel state */
	chan_data->configured = false;
	chan_data->active = false;
	chan_data->suspended = false;
	chan_data->cyclic_active = false;
	chan_data->block_index = 0;
	chan_data->block_count = 0;
	/* Clear blocks array */
	memset(chan_data->blocks, 0, sizeof(chan_data->blocks));

	k_mutex_unlock(&data->lock);
}

static int tt_bh_dma_noc_init(const struct device *dev)
{
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;
	const struct tt_bh_dma_noc_config *cfg = (const struct tt_bh_dma_noc_config *)dev->config;

	k_mutex_init(&data->lock);

	/* Initialize work context for all channels */
	for (uint8_t i = 0; i < cfg->num_channels; ++i) {
		cfg->channels[i].work_ctx.dev = dev;
		cfg->channels[i].work_ctx.channel = i;
		k_work_init_delayable(&cfg->channels[i].work_ctx.work, noc_dma_memcpy_work);
	}

	return 0;
}

static int tt_bh_dma_noc_get_status(const struct device *dev, uint32_t channel,
				    struct dma_status *status)
{
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!status) {
		LOG_ERR("Status pointer is NULL");
		return -EINVAL;
	}

	/* Set transfer direction from configuration */
	if (chan_data->configured) {
		status->dir = (enum dma_channel_direction)chan_data->config.channel_direction;
	} else {
		status->dir = MEMORY_TO_MEMORY; /* Default direction */
	}

	/* Determine if channel is busy */
	status->busy = chan_data->active && !chan_data->suspended;

	/* Calculate pending length and total copied based on block progress */
	if (chan_data->configured) {
		uint32_t remaining_bytes = 0;
		uint32_t completed_bytes = 0;

		/* Sum up completed block sizes */
		for (int i = 0; i < chan_data->block_index && i < chan_data->block_count; i++) {
			completed_bytes += chan_data->blocks[i].block_size;
		}

		/* Sum up remaining block sizes */
		for (int i = chan_data->block_index; i < chan_data->block_count; i++) {
			remaining_bytes += chan_data->blocks[i].block_size;
		}

		status->pending_length = chan_data->active ? remaining_bytes : 0;
		status->total_copied = completed_bytes;
	} else {
		status->pending_length = 0;
		status->total_copied = 0;
	}

	/* For cyclic transfers, provide additional information */
	if (chan_data->config.cyclic && chan_data->cyclic_active) {
		/* In cyclic mode, we can provide current position information */
		if (chan_data->block_count > 0) {
			/* Calculate position within the cyclic buffer */
			uint32_t total_buffer_size = 0;
			uint32_t current_position = 0;

			/* Calculate total buffer size */
			for (int i = 0; i < chan_data->block_count; i++) {
				total_buffer_size += chan_data->blocks[i].block_size;
			}

			/* Calculate current position (block_index may wrap in cyclic mode) */
			int current_index = chan_data->block_index % chan_data->block_count;

			for (int i = 0; i < current_index; i++) {
				current_position += chan_data->blocks[i].block_size;
			}

			status->read_position = current_position;
			status->write_position = current_position;
			status->free = total_buffer_size - current_position;
		}
	}

	return 0;
}

static const struct dma_driver_api tt_bh_dma_noc_api = {
	.config = tt_bh_dma_noc_config,
	.reload = NULL,
	.start = tt_bh_dma_noc_start,
	.stop = tt_bh_dma_noc_stop,
	.suspend = tt_bh_dma_noc_suspend,
	.resume = tt_bh_dma_noc_resume,
	.get_status = tt_bh_dma_noc_get_status,
	.get_attribute = NULL,
	.chan_filter = NULL,
	.chan_release = tt_bh_dma_noc_release_channel,
};

#define TT_BH_DMA_NOC_INIT(inst)                                                                   \
	static struct tt_bh_dma_channel_data                                                       \
		tt_bh_dma_noc_channels_##inst[DT_INST_PROP(inst, dma_channels)];                   \
                                                                                                   \
	static const struct tt_bh_dma_noc_config noc_dma_config_##inst = {                         \
		.channels = tt_bh_dma_noc_channels_##inst,                                         \
		.num_channels = DT_INST_PROP(inst, dma_channels),                                  \
	};                                                                                         \
                                                                                                   \
	static struct tt_bh_dma_noc_data noc_dma_data_##inst = {};                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &tt_bh_dma_noc_init, NULL, &noc_dma_data_##inst,               \
			      &noc_dma_config_##inst, POST_KERNEL, CONFIG_DMA_INIT_PRIORITY,       \
			      &tt_bh_dma_noc_api);

DT_INST_FOREACH_STATUS_OKAY(TT_BH_DMA_NOC_INIT)
