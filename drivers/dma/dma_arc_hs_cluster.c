/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>

#include "dma_arc_hs_cluster.h"

#define DT_DRV_COMPAT snps_designware_dma_arc

LOG_MODULE_REGISTER(dma_arc, CONFIG_DMA_LOG_LEVEL);

#define ARC_DMA_MAX_CHANNELS    16
#define ARC_DMA_MAX_DESCRIPTORS 256
#define ARC_DMA_ATOMIC_WORDS    ATOMIC_BITMAP_SIZE(ARC_DMA_MAX_CHANNELS)

struct arc_dma_channel {
	uint32_t id;
	bool in_use;
	bool active;
	dma_callback_t callback;
	void *callback_arg;
	struct dma_config config;
	uint32_t handle;
};

struct arc_dma_config {
	uint32_t base;
	uint32_t channels;
	uint32_t descriptors;
	uint32_t max_burst_size;
	uint32_t max_pending_transactions;
	uint32_t buffer_size;
	bool coherency_support;
};

struct arc_dma_data {
	struct dma_context dma_ctx;
	struct arc_dma_channel channels[ARC_DMA_MAX_CHANNELS];
	atomic_t channels_atomic[ARC_DMA_ATOMIC_WORDS];
	struct k_spinlock lock;
};

static int arc_dma_config(const struct device *dev, uint32_t channel, struct dma_config *config)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	if (!config) {
		LOG_ERR("Invalid config");
		return -EINVAL;
	}

	if (config->block_count != 1) {
		LOG_ERR("Only single block transfers supported");
		return -ENOTSUP;
	}

	if (config->channel_direction != MEMORY_TO_MEMORY) {
		LOG_ERR("Only memory-to-memory transfers supported");
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use) {
		LOG_ERR("Channel %u not allocated", channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	chan->config = *config;
	chan->callback = config->dma_callback;
	chan->callback_arg = config->user_data;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Configured channel %u", channel);
	return 0;
}

static int arc_dma_start(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	struct dma_block_config *block;
	uint32_t attr;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use) {
		LOG_ERR("Channel %u not allocated", channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	if (chan->active) {
		LOG_WRN("Channel %u already active", channel);
		k_spin_unlock(&data->lock, key);
		return 0;
	}

	block = chan->config.head_block;
	if (!block) {
		LOG_ERR("No block configuration for channel %u", channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	attr = ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;

	arc_dma_start_hw(channel, (const void *)block->source_address, (void *)block->dest_address,
			 block->block_size, attr);

	chan->handle = arc_dma_get_handle_hw();
	chan->active = true;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Started DMA transfer on channel %u, handle %u", channel, chan->handle);
	return 0;
}

static int arc_dma_stop(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use) {
		LOG_ERR("Channel %u not allocated", channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	if (!chan->active) {
		LOG_WRN("Channel %u already stopped", channel);
		k_spin_unlock(&data->lock, key);
		return 0;
	}

	chan->active = false;
	arc_dma_clear_done_hw(chan->handle);

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Stopped DMA transfer on channel %u", channel);
	return 0;
}

static int arc_dma_get_status(const struct device *dev, uint32_t channel, struct dma_status *stat)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	uint32_t done_status;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		return -EINVAL;
	}

	if (!stat) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use) {
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	stat->pending_length = 0;
	stat->dir = MEMORY_TO_MEMORY;
	stat->busy = false;

	if (chan->active) {
		done_status = arc_dma_get_done_hw(chan->handle);
		if (done_status == 0) {
			stat->busy = true;
			if (chan->config.head_block) {
				stat->pending_length = chan->config.head_block->block_size;
			}
		} else {
			chan->active = false;
			arc_dma_clear_done_hw(chan->handle);

			if (chan->callback) {
				chan->callback(dev, chan->callback_arg, channel, 0);
			}
		}
	}

	k_spin_unlock(&data->lock, key);
	return 0;
}

static bool arc_dma_chan_filter(const struct device *dev, int channel, void *filter_param)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key;
	bool result = false;

	ARG_UNUSED(filter_param);

	if (channel >= dev_config->channels) {
		return false;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use) {
		chan->in_use = true;
		result = true;
	}

	k_spin_unlock(&data->lock, key);

	if (result) {
		LOG_DBG("Allocated channel %d", channel);
	}

	return result;
}

static void arc_dma_chan_release(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		return;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (chan->active) {
		chan->active = false;
		arc_dma_clear_done_hw(chan->handle);
	}

	chan->in_use = false;
	memset(&chan->config, 0, sizeof(chan->config));
	chan->callback = NULL;
	chan->callback_arg = NULL;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Released channel %u", channel);
}

static int arc_dma_get_attribute(const struct device *dev, uint32_t type, uint32_t *value)
{
	switch (type) {
	case DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT:
		*value = 4; /* 32-bit aligned */
		break;
	case DMA_ATTR_BUFFER_SIZE_ALIGNMENT:
		*value = 4; /* 32-bit aligned */
		break;
	case DMA_ATTR_COPY_ALIGNMENT:
		*value = 4; /* 32-bit aligned */
		break;
	case DMA_ATTR_MAX_BLOCK_COUNT:
		*value = 1; /* Single block only */
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static const struct dma_driver_api arc_dma_api = {
	.config = arc_dma_config,
	.start = arc_dma_start,
	.stop = arc_dma_stop,
	.get_status = arc_dma_get_status,
	.chan_filter = arc_dma_chan_filter,
	.chan_release = arc_dma_chan_release,
	.get_attribute = arc_dma_get_attribute,
};

static int arc_dma_init(const struct device *dev)
{
	const struct arc_dma_config *config = dev->config;
	struct arc_dma_data *data = dev->data;
	int i;

	LOG_DBG("Initializing ARC DMA with %u channels", config->channels);

	data->dma_ctx.magic = DMA_MAGIC;
	data->dma_ctx.dma_channels = config->channels;
	data->dma_ctx.atomic = data->channels_atomic;
	memset(data->channels_atomic, 0, sizeof(data->channels_atomic));

	for (i = 0; i < config->channels; i++) {
		data->channels[i].id = i;
		data->channels[i].in_use = false;
		data->channels[i].active = false;
		data->channels[i].callback = NULL;
		data->channels[i].callback_arg = NULL;
	}

	arc_dma_config_hw();

	for (i = 0; i < config->channels; i++) {
		arc_dma_init_channel_hw(i, 0, config->descriptors - 1);
	}

	LOG_INF("ARC DMA initialized successfully");
	return 0;
}

#define ARC_DMA_INIT(inst)                                                                         \
	static const struct arc_dma_config arc_dma_config_##inst = {                               \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.channels = DT_INST_PROP_OR(inst, dma_channels, 1),                                \
		.descriptors = DT_INST_PROP_OR(inst, dma_descriptors, 32),                         \
		.max_burst_size = DT_INST_PROP_OR(inst, max_burst_size, 4),                        \
		.max_pending_transactions = DT_INST_PROP_OR(inst, max_pending_transactions, 4),    \
		.buffer_size = DT_INST_PROP_OR(inst, buffer_size, 16),                             \
		.coherency_support = DT_INST_PROP_OR(inst, coherency_support, false),              \
	};                                                                                         \
                                                                                                   \
	static struct arc_dma_data arc_dma_data_##inst;                                            \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, arc_dma_init, NULL, &arc_dma_data_##inst,                      \
			      &arc_dma_config_##inst, POST_KERNEL, CONFIG_DMA_INIT_PRIORITY,       \
			      &arc_dma_api);

DT_INST_FOREACH_STATUS_OKAY(ARC_DMA_INIT)
