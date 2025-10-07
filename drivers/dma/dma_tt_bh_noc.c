/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_noc_dma

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma/dma_noc_tt_bh.h>
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

/* Forward declarations */
static int noc_dma_start(const struct device *dev, uint32_t channel);
static bool is_local_memory_transfer(struct dma_config *config,
				     struct tt_bh_dma_noc_coords *coords);

struct tt_bh_dma_noc_config {
	uint32_t max_channels;
};

struct tt_bh_dma_channel_data {
	struct dma_block_config block;
	struct tt_bh_dma_noc_coords coords;
	struct dma_config config;
	struct noc_dma_work_context work_ctx;
	bool active;
	bool configured;
	bool suspended;
	bool cyclic_active;
	uint32_t transfer_count;
};

struct tt_bh_dma_noc_data {
	const struct device *dev;
	struct tt_bh_dma_channel_data channels[CONFIG_DMA_TT_NOC_MAX_CHANNELS];
	struct k_mutex lock;
	uint32_t channel_allocator; /* Bitmap for dynamic channel allocation */
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

static inline void program_noc_dma_tlb(uint8_t x, uint8_t y)
{
	uint32_t addr = TARGET_ADDR_LO;

	NOC2AXITlbSetup(NOC_DMA_NOC_ID, NOC_DMA_TLB, x, y, addr);
}

/* program_noc_dma_tlb must be invoked before this func call */
static inline void write_noc_dma_config(uint32_t addr, uint32_t value)
{
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, addr, value);
}

/* program noc_dma_tlb must be invoked before this func call */
static inline uint32_t read_noc_dma_config(uint32_t addr)
{
	return NOC2AXIRead32(NOC_DMA_NOC_ID, NOC_DMA_TLB, addr);
}

static bool noc_wait_cmd_ready(void)
{
#ifdef CONFIG_BOARD_NATIVE_SIM
	/* Fake completion */
	return true;
#else
	uint32_t cmd_ctrl;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(NOC_DMA_TIMEOUT_MS));

	do {
		cmd_ctrl = read_noc_dma_config(CMD_CTRL);
	} while (cmd_ctrl != 0 && !sys_timepoint_expired(timeout));

	return cmd_ctrl == 0;
#endif
}

static uint32_t get_expected_acks(uint32_t noc_cmd, uint64_t size)
{
	uint32_t ack_reg_addr =
		(noc_cmd & NOC_CMD_WR) ? NIU_MST_WR_ACK_RECEIVED : NIU_MST_RD_RESP_RECEIVED;
	uint32_t packet_received = read_noc_dma_config(ack_reg_addr);
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
		ack_received = read_noc_dma_config(ack_reg_addr);
		behind = is_behind(ack_received, expected_acks);
	} while (behind && !sys_timepoint_expired(timeout));

	return !behind;
#endif
}

static uint32_t noc_dma_format_coord(uint8_t x, uint8_t y)
{
	/* clang-format off */
	return (union ret_addr_hi_u){
		.f = {.end_x = x, .end_y = y}
	}.u;
	/* clang-format on */
}

static bool noc_dma_transfer(uint32_t cmd, uint32_t ret_coord, uint64_t ret_addr,
			     uint32_t targ_coord, uint64_t targ_addr, uint32_t size, bool multicast,
			     uint8_t transaction_id, bool include_self, bool wait_for_done)
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
	uint32_t expected_acks;

	if (multicast) {
		noc_ctrl |= NOC_CMD_PATH_RESERVE | NOC_CMD_BRCST_PACKET;

		if (include_self) {
			noc_ctrl |= NOC_CMD_BRCST_SRC_INCLUDE;
		}
	}

	if (wait_for_done) {
		noc_ctrl |= NOC_CMD_RESP_MARKED;
		expected_acks = get_expected_acks(noc_ctrl, size);
	}

	if (!noc_wait_cmd_ready()) {
		return false;
	}

	write_noc_dma_config(TARGET_ADDR_LO, targ_addr_lo);
	write_noc_dma_config(TARGET_ADDR_MID, targ_addr_mid);
	write_noc_dma_config(TARGET_ADDR_HI, targ_addr_hi);
	write_noc_dma_config(RET_ADDR_LO, ret_addr_lo);
	write_noc_dma_config(RET_ADDR_MID, ret_addr_mid);
	write_noc_dma_config(RET_ADDR_HI, ret_addr_hi);
	write_noc_dma_config(PACKET_TAG, noc_packet_tag);
	write_noc_dma_config(AT_LEN, noc_at_len_be);
	write_noc_dma_config(AT_LEN_1, 0);
	write_noc_dma_config(AT_DATA, 0);
	write_noc_dma_config(BRCST_EXCLUDE, 0);
	write_noc_dma_config(CMD_BRCST, noc_ctrl);
	write_noc_dma_config(CMD_CTRL, 1);

	if (wait_for_done && !wait_noc_dma_done(noc_ctrl, expected_acks)) {
		return false;
	}

	return true;
}

/* Async work handler for memory-to-memory transfers (ARC to ARC within TLB window) */
static void noc_dma_memcpy_work(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct noc_dma_work_context *ctx = CONTAINER_OF(dwork, struct noc_dma_work_context, work);
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)ctx->dev->data;
	struct tt_bh_dma_channel_data *chan_data = &data->channels[ctx->channel];

	/* Check if transfer is suspended */
	if (chan_data->suspended) {
		/* Re-schedule to check again later */
		k_work_reschedule(&chan_data->work_ctx.work, K_MSEC(1));
		return;
	}

	/* Determine transfer type and perform accordingly */
	if (is_local_memory_transfer(&chan_data->config, &chan_data->coords)) {
		/* Local memory copy */
		memcpy((void *)(uintptr_t)chan_data->block.dest_address,
		       (void *)(uintptr_t)chan_data->block.source_address,
		       chan_data->block.block_size);
	} else {
		/* NOC transfer */
		uint32_t ret_coord = noc_dma_format_coord(chan_data->coords.source_x,
							  chan_data->coords.source_y);
		uint64_t ret_addr = chan_data->block.source_address;

		uint32_t targ_coord =
			noc_dma_format_coord(chan_data->coords.dest_x, chan_data->coords.dest_y);
		uint64_t targ_addr = chan_data->block.dest_address;

		program_noc_dma_tlb(chan_data->coords.dest_x, chan_data->coords.dest_y);

		bool success =
			noc_dma_transfer(NOC_CMD_WR, ret_coord, ret_addr, targ_coord, targ_addr,
					 chan_data->block.block_size, false, 0, false, true);

		if (!success) {
			LOG_ERR("NOC DMA transfer failed on channel %u", ctx->channel);
			chan_data->active = false;
			return;
		}
	}

	chan_data->transfer_count++;

	/* Call completion callback if there's a callback function */
	if (chan_data->config.dma_callback) {
		chan_data->config.dma_callback(ctx->dev, chan_data->config.user_data, ctx->channel,
					       0);
	}

	/* Handle cyclic transfers */
	if (chan_data->config.cyclic && chan_data->cyclic_active) {
		/* For cyclic transfers, reschedule the work to repeat */
		k_work_reschedule(&chan_data->work_ctx.work, K_MSEC(1));
		return;
	}

	/* Handle channel linking for non-cyclic transfers */
	if (chan_data->config.linked_channel != DMA_CHANNEL_INVALID) {
		uint32_t linked_chan = chan_data->config.linked_channel;

		if (linked_chan < CONFIG_DMA_TT_NOC_MAX_CHANNELS &&
		    data->channels[linked_chan].configured) {

			/* Trigger linked channel based on chaining configuration */
			bool trigger_linked = false;

			if (chan_data->config.dest_chaining_en) {
				/* Major loop link - trigger at end of block */
				trigger_linked = true;
			}
			if (chan_data->config.source_chaining_en) {
				/* Minor loop link - trigger during transfer */
				trigger_linked = true;
			}

			if (trigger_linked) {
				LOG_DBG("Triggering linked channel %u from channel %u", linked_chan,
					ctx->channel);
				noc_dma_start(ctx->dev, linked_chan);
			}
		}
	}

	/* Mark as inactive if not cyclic */
	if (!chan_data->config.cyclic || !chan_data->cyclic_active) {
		chan_data->active = false;
	}
}

/*
 * Check if this is a memory-to-memory transfer within the same TLB window
 * (ARC to ARC case that should be handled as memcpy)
 */
static bool is_local_memory_transfer(struct dma_config *config, struct tt_bh_dma_noc_coords *coords)
{
	/* If it's explicitly a MEMORY_TO_MEMORY transfer, use local memcpy */
	if (config->channel_direction == MEMORY_TO_MEMORY) {
		return true;
	}

	/* If no coordinates provided for non-M2M transfers, assume local */
	if (coords == NULL) {
		return true;
	}

	/* If source and dest coordinates are the same, it's local */
	return (coords->source_x == coords->dest_x && coords->source_y == coords->dest_y);
}

static struct tt_bh_dma_channel_data *get_channel_data(const struct device *dev, uint32_t channel)
{
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;

	if (channel >= CONFIG_DMA_TT_NOC_MAX_CHANNELS) {
		return NULL;
	}

	return &data->channels[channel];
}

/*
 * Config the source and dest NOC coordinates, the source and dest addresses and
 * the size of data transfer.
 *
 * Transfer data using only one block for simplicity.
 */
static int noc_dma_config(const struct device *dev, uint32_t channel, struct dma_config *config)
{
	struct dma_block_config *block = config->head_block;
	struct tt_bh_dma_noc_coords *coords = (struct tt_bh_dma_noc_coords *)config->user_data;
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;
	struct tt_bh_dma_channel_data *chan_data = get_channel_data(dev, channel);

	if (!chan_data) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!block) {
		LOG_ERR("No block configuration provided");
		return -EINVAL;
	}

	/* Validate configuration */
	if (config->block_count != 1) {
		LOG_ERR("Only single block transfers supported");
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	chan_data->block = *block;
	chan_data->config = *config;
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

static int noc_dma_start(const struct device *dev, uint32_t channel)
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
	chan_data->transfer_count = 0;

	/* Enable cyclic mode if configured */
	if (chan_data->config.cyclic) {
		chan_data->cyclic_active = true;
	}

	/* Check if this is a local memory-to-memory transfer */
	if (is_local_memory_transfer(&chan_data->config, &chan_data->coords)) {
		LOG_DBG("Performing local memory copy (ARC to ARC) on channel %u", channel);

		/* Always use async operation to ensure callbacks are properly triggered */
		k_work_reschedule(&chan_data->work_ctx.work, K_NO_WAIT);
		return 0;
	}

	/* NOC transfer between different coordinates - also use async for consistency */
	LOG_DBG("Performing NOC transfer from (%u,%u) to (%u,%u) on channel %u",
		chan_data->coords.source_x, chan_data->coords.source_y, chan_data->coords.dest_x,
		chan_data->coords.dest_y, channel);

	/* Use async worker for NOC transfers as well to maintain callback consistency */
	k_work_reschedule(&chan_data->work_ctx.work, K_NO_WAIT);
	return 0;
}

static int noc_dma_init(const struct device *dev)
{
	struct tt_bh_dma_noc_data *data = (struct tt_bh_dma_noc_data *)dev->data;

	/* Initialize mutex for thread safety */
	k_mutex_init(&data->lock);

	/* Initialize device reference */
	data->dev = dev;

	/* Initialize channel allocator bitmap */
	data->channel_allocator = 0;

	/* Initialize all channels */
	for (int i = 0; i < CONFIG_DMA_TT_NOC_MAX_CHANNELS; i++) {
		data->channels[i].configured = false;
		data->channels[i].active = false;
		data->channels[i].suspended = false;
		data->channels[i].cyclic_active = false;
		data->channels[i].transfer_count = 0;
		/* Initialize delayed work for each channel */
		data->channels[i].work_ctx.dev = dev;
		data->channels[i].work_ctx.channel = i;
		k_work_init_delayable(&data->channels[i].work_ctx.work, noc_dma_memcpy_work);
	}

	return 0;
}

static int noc_dma_stop(const struct device *dev, uint32_t channel)
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

	return 0;
}

static int noc_dma_suspend(const struct device *dev, uint32_t channel)
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

static int noc_dma_resume(const struct device *dev, uint32_t channel)
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

static void noc_dma_release_channel(const struct device *dev, uint32_t channel)
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
		noc_dma_stop(dev, channel);
	}

	/* Reset channel state */
	chan_data->configured = false;
	chan_data->active = false;
	chan_data->suspended = false;
	chan_data->cyclic_active = false;
	chan_data->transfer_count = 0;

	/* Mark channel as free (safe even if not previously allocated) */
	data->channel_allocator &= ~BIT(channel);

	k_mutex_unlock(&data->lock);

	LOG_DBG("Released channel %u", channel);
}

static const struct dma_driver_api noc_dma_api = {
	.config = noc_dma_config,
	.reload = NULL,
	.start = noc_dma_start,
	.stop = noc_dma_stop,
	.suspend = noc_dma_suspend,
	.resume = noc_dma_resume,
	.get_status = NULL,
	.get_attribute = NULL,
	.chan_filter = NULL,
	.chan_release = noc_dma_release_channel,
};

#define NOC_DMA_INIT(n)                                                                            \
	static const struct tt_bh_dma_noc_config noc_dma_config_##n = {                            \
		.max_channels = CONFIG_DMA_TT_NOC_MAX_CHANNELS,                                    \
	};                                                                                         \
	static struct tt_bh_dma_noc_data noc_dma_data_##n = {};                                    \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &noc_dma_init, NULL, &noc_dma_data_##n, &noc_dma_config_##n,      \
			      POST_KERNEL, CONFIG_DMA_INIT_PRIORITY, &noc_dma_api);

DT_INST_FOREACH_STATUS_OKAY(NOC_DMA_INIT)
