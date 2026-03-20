/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_grendel_smc_dma

#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#include "smc_cpu_reg.h"

LOG_MODULE_REGISTER(dma_tt_grendel_smc, CONFIG_DMA_LOG_LEVEL);

#define DMA_REG_CONFIG            0x00
#define DMA_REG_STATUS(ch)        (0x04 + (ch) * 4)
#define DMA_REG_NEXT_ID(ch)       (0x48 + (ch) * 8)
#define DMA_REG_DONE(ch)          (0xC8 + (ch) * 4)
#define DMA_REG_DST_ADDR_LO       0x108
#define DMA_REG_DST_ADDR_HI       0x10C
#define DMA_REG_SRC_ADDR_LO       0x110
#define DMA_REG_SRC_ADDR_HI       0x114
#define DMA_REG_LENGTH_LO         0x118
#define DMA_REG_LENGTH_HI         0x11C
#define DMA_REG_DST_STRIDE_LO     0x120
#define DMA_REG_DST_STRIDE_HI     0x124
#define DMA_REG_SRC_STRIDE_LO     0x128
#define DMA_REG_SRC_STRIDE_HI     0x12C
#define DMA_REG_NUM_REPS_LO       0x130
#define DMA_REG_NUM_REPS_HI       0x134

#define DMA_TIMEOUT_MS            1000

struct dma_grendel_channel_data {
	struct dma_config cfg;
	uint32_t transfer_id;
	bool configured;
};

struct dma_grendel_config {
	uintptr_t base;
	struct dma_grendel_channel_data *channels;
	uint8_t num_channels;
};

struct dma_grendel_data {
	struct k_spinlock lock;
};

static void dma_program_transfer(uintptr_t base, const struct dma_block_config *block)
{
	uint64_t src = block->source_address;
	uint64_t dst = block->dest_address;
	uint64_t len = block->block_size;
	uint32_t reps = block->source_gather_count;

	AXI_DMA_CTRL_SRC_ADDRESS_LO_reg_u src_lo = { .f.data = (uint32_t)src };
	AXI_DMA_CTRL_SRC_ADDRESS_HI_reg_u src_hi = { .f.data = (uint32_t)(src >> 32) };
	AXI_DMA_CTRL_DST_ADDRESS_LO_reg_u dst_lo = { .f.data = (uint32_t)dst };
	AXI_DMA_CTRL_DST_ADDRESS_HI_reg_u dst_hi = { .f.data = (uint32_t)(dst >> 32) };
	AXI_DMA_CTRL_LENGTH_LO_reg_u len_lo = { .f.data = (uint32_t)len };
	AXI_DMA_CTRL_LENGTH_HI_reg_u len_hi = { .f.data = (uint32_t)(len >> 32) };
	AXI_DMA_CTRL_DST_STRIDE_LO_reg_u dst_stride_lo = {
		.f.data = (uint32_t)block->dest_scatter_interval
	};
	AXI_DMA_CTRL_DST_STRIDE_HI_reg_u dst_stride_hi = { .val = 0 };
	AXI_DMA_CTRL_SRC_STRIDE_LO_reg_u src_stride_lo = {
		.f.data = (uint32_t)block->source_gather_interval
	};
	AXI_DMA_CTRL_SRC_STRIDE_HI_reg_u src_stride_hi = { .val = 0 };
	/* A single transfer requires num_repetitions = 1, not 0. */
	AXI_DMA_CTRL_NUM_REPETITIONS_LO_reg_u reps_lo = {
		.f.data = reps > 0 ? reps : 1
	};
	AXI_DMA_CTRL_NUM_REPETITIONS_HI_reg_u reps_hi = { .val = 0 };

	sys_write32(src_lo.val, base + DMA_REG_SRC_ADDR_LO);
	sys_write32(src_hi.val, base + DMA_REG_SRC_ADDR_HI);
	sys_write32(dst_lo.val, base + DMA_REG_DST_ADDR_LO);
	sys_write32(dst_hi.val, base + DMA_REG_DST_ADDR_HI);
	sys_write32(len_lo.val, base + DMA_REG_LENGTH_LO);
	sys_write32(len_hi.val, base + DMA_REG_LENGTH_HI);
	sys_write32(dst_stride_lo.val, base + DMA_REG_DST_STRIDE_LO);
	sys_write32(dst_stride_hi.val, base + DMA_REG_DST_STRIDE_HI);
	sys_write32(src_stride_lo.val, base + DMA_REG_SRC_STRIDE_LO);
	sys_write32(src_stride_hi.val, base + DMA_REG_SRC_STRIDE_HI);
	sys_write32(reps_lo.val, base + DMA_REG_NUM_REPS_LO);
	sys_write32(reps_hi.val, base + DMA_REG_NUM_REPS_HI);
}

static int dma_grendel_config(const struct device *dev, uint32_t channel,
			      struct dma_config *config)
{
	const struct dma_grendel_config *cfg = dev->config;
	struct dma_grendel_data *data = dev->data;

	if (channel >= cfg->num_channels) {
		LOG_ERR("Invalid channel %u (max %u)", channel, cfg->num_channels - 1);
		return -EINVAL;
	}

	if (config->channel_direction != MEMORY_TO_MEMORY) {
		LOG_ERR("Only MEMORY_TO_MEMORY transfers are supported");
		return -ENOTSUP;
	}

	if (config->block_count == 0 || config->head_block == NULL) {
		LOG_ERR("No block configuration provided");
		return -EINVAL;
	}

	struct dma_grendel_channel_data *ch = &cfg->channels[channel];
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	ch->cfg = *config;
	ch->transfer_id = 0;
	ch->configured = true;

	k_spin_unlock(&data->lock, key);
	return 0;
}

static int dma_grendel_start(const struct device *dev, uint32_t channel)
{
	const struct dma_grendel_config *cfg = dev->config;
	struct dma_grendel_data *data = dev->data;

	if (channel >= cfg->num_channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	struct dma_grendel_channel_data *ch = &cfg->channels[channel];

	if (!ch->configured) {
		LOG_ERR("Channel %u not configured", channel);
		return -EINVAL;
	}

	const struct dma_block_config *block = ch->cfg.head_block;

	for (uint32_t i = 0; i < ch->cfg.block_count && block != NULL; i++) {
		k_spinlock_key_t key = k_spin_lock(&data->lock);

		dma_program_transfer(cfg->base, block);
		AXI_DMA_CTRL_NEXT_ID_reg_u next_id;

		next_id.val = sys_read32(cfg->base + DMA_REG_NEXT_ID(channel));

		k_spin_unlock(&data->lock, key);

		if (next_id.f.data == 0) {
			LOG_ERR("DMA launch failed on channel %u block %u", channel, i);
			return -EIO;
		}

		ch->transfer_id = next_id.f.data;

		k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(DMA_TIMEOUT_MS));
		AXI_DMA_CTRL_DONE_reg_u done;

		do {
			done.val = sys_read32(cfg->base + DMA_REG_DONE(channel));
			if (done.f.data >= next_id.f.data) {
				break;
			}
		} while (!sys_timepoint_expired(timeout));

		if (done.f.data < next_id.f.data) {
			LOG_ERR("DMA ch%u timed out (id=%u, done=%u)", channel, next_id.f.data, done.f.data);
			return -ETIMEDOUT;
		}

		block = block->next_block;
	}

	if (ch->cfg.dma_callback) {
		ch->cfg.dma_callback(dev, ch->cfg.user_data, channel,
				     DMA_STATUS_COMPLETE);
	}

	return 0;
}

static int dma_grendel_stop(const struct device *dev, uint32_t channel)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);

	return 0;
}

static int dma_grendel_get_status(const struct device *dev, uint32_t channel,
				  struct dma_status *status)
{
	const struct dma_grendel_config *cfg = dev->config;

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	struct dma_grendel_channel_data *ch = &cfg->channels[channel];
	AXI_DMA_CTRL_STATUS_reg_u hw_status;
	AXI_DMA_CTRL_DONE_reg_u done;

	hw_status.val = sys_read32(cfg->base + DMA_REG_STATUS(channel));
	status->busy = hw_status.f.busy != 0;

	if (ch->transfer_id > 0) {
		done.val = sys_read32(cfg->base + DMA_REG_DONE(channel));
		status->pending_length = (done.f.data < ch->transfer_id)
					 ? ch->cfg.head_block->block_size : 0;
	} else {
		status->pending_length = 0;
	}

	return 0;
}

static int dma_grendel_init(const struct device *dev)
{
	const struct dma_grendel_config *cfg = dev->config;
	SMC_CPU_CTRL_RESET_CTRL_reg_u reset_ctrl;
	AXI_DMA_CTRL_CONFIG_reg_u config = { .f.enabled_nd = 1 };
	AXI_DMA_CTRL_NUM_REPETITIONS_LO_reg_u reps = { .f.data = 1 };

	/* Release DMA from reset */
	reset_ctrl.val = sys_read32(SMC_CPU_CTRL_RESET_CTRL_REG_ADDR);
	reset_ctrl.f.dma_reset_n_n0_scan = 1;
	sys_write32(reset_ctrl.val, SMC_CPU_CTRL_RESET_CTRL_REG_ADDR);



	sys_write32(config.val, cfg->base + DMA_REG_CONFIG);
	sys_write32(reps.val, cfg->base + DMA_REG_NUM_REPS_LO);

	return 0;
}

static const struct dma_driver_api dma_grendel_api = {
	.config = dma_grendel_config,
	.reload = NULL,
	.start = dma_grendel_start,
	.stop = dma_grendel_stop,
	.suspend = NULL,
	.resume = NULL,
	.get_status = dma_grendel_get_status,
	.get_attribute = NULL,
	.chan_filter = NULL,
	.chan_release = NULL,
};

#define DMA_GRENDEL_INIT(inst)                                                                     \
	static struct dma_grendel_channel_data                                                     \
		dma_grendel_channels_##inst[DT_INST_PROP(inst, dma_channels)];                     \
                                                                                                   \
	static const struct dma_grendel_config dma_grendel_config_##inst = {                       \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.channels = dma_grendel_channels_##inst,                                           \
		.num_channels = DT_INST_PROP(inst, dma_channels),                                  \
	};                                                                                         \
                                                                                                   \
	static struct dma_grendel_data dma_grendel_data_##inst = {};                               \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &dma_grendel_init, NULL, &dma_grendel_data_##inst,             \
			      &dma_grendel_config_##inst, POST_KERNEL,                             \
			      CONFIG_DMA_INIT_PRIORITY, &dma_grendel_api);

DT_INST_FOREACH_STATUS_OKAY(DMA_GRENDEL_INIT)
