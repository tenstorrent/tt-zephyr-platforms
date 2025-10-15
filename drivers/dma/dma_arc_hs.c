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

/* ARC DMA Auxiliary Register Definitions */
#define DMA_AUX_BASE     (0xd00)
#define DMA_C_CTRL_AUX   (0xd00 + 0x0)
#define DMA_C_CHAN_AUX   (0xd00 + 0x1)
#define DMA_C_SRC_AUX    (0xd00 + 0x2)
#define DMA_C_SRC_HI_AUX (0xd00 + 0x3)
#define DMA_C_DST_AUX    (0xd00 + 0x4)
#define DMA_C_DST_HI_AUX (0xd00 + 0x5)
#define DMA_C_ATTR_AUX   (0xd00 + 0x6)
#define DMA_C_LEN_AUX    (0xd00 + 0x7)
#define DMA_C_HANDLE_AUX (0xd00 + 0x8)
#define DMA_C_STAT_AUX   (0xd00 + 0xc)

#define DMA_S_CTRL_AUX      (0xd00 + 0x10)
#define DMA_S_BASEC_AUX(ch) (0xd00 + 0x83 + ((ch) * 8))
#define DMA_S_LASTC_AUX(ch) (0xd00 + 0x84 + ((ch) * 8))
#define DMA_S_STATC_AUX(ch) (0xd00 + 0x86 + ((ch) * 8))
#define DMA_S_DONESTATD_AUX(d)                                                                     \
	(0xd00 + 0x20 + (d)) /* Descriptor selection. Each D stores descriptors d*32 +: 32 */
#define DMA_S_DONESTATD_CLR_AUX(d) (0xd00 + 0x40 + (d))

/* ARC DMA Attribute Flags */
#define ARC_DMA_NP_ATTR       (1 << 3) /* Enable non posted writes */
#define ARC_DMA_SET_DONE_ATTR (1 << 0) /* Set done without triggering interrupt */

/* ARC Auxiliary Register Access Functions */
static inline void arc_write_aux(uint32_t reg, uint32_t val)
{
	__asm__ volatile("sr %0, [%1]" : : "r"(val), "r"(reg) : "memory");
}

static inline uint32_t arc_read_aux(uint32_t reg)
{
	uint32_t val;

	__asm__ volatile("lr %0, [%1]" : "=r"(val) : "r"(reg) : "memory");
	return val;
}

/* Low-level ARC DMA Functions */
static inline void dma_arc_hs_config_hw(void)
{
	uint32_t reg = 0;

	reg = (0xf << 4);                   /* Set LBU read transaction limit to max */
	reg |= (0x4 << 8);                  /* Set max burst length to 16 (max supported) */
	arc_write_aux(DMA_S_CTRL_AUX, reg); /* Apply settings above */
}

static inline void dma_arc_hs_init_channel_hw(uint32_t dma_ch, uint32_t base, uint32_t last)
{
	arc_write_aux(DMA_S_BASEC_AUX(dma_ch), base);
	arc_write_aux(DMA_S_LASTC_AUX(dma_ch), last);
	arc_write_aux(DMA_S_STATC_AUX(dma_ch), 0x1); /* Enable dma_ch */
}

static inline void dma_arc_hs_start_hw(uint32_t dma_ch, const void *p_src, void *p_dst,
				       uint32_t len, uint32_t attr)
{
	arc_write_aux(DMA_C_CHAN_AUX, dma_ch);
	arc_write_aux(DMA_C_SRC_AUX, (uint32_t)p_src);
	arc_write_aux(DMA_C_DST_AUX, (uint32_t)p_dst);
	arc_write_aux(DMA_C_ATTR_AUX, attr);
	arc_write_aux(DMA_C_LEN_AUX, len);
}

static inline uint32_t dma_arc_hs_get_handle_hw(void)
{
	return arc_read_aux(DMA_C_HANDLE_AUX);
}

static inline uint32_t dma_arc_hs_poll_busy_hw(void)
{
	return arc_read_aux(DMA_C_STAT_AUX);
}

static inline void dma_arc_hs_clear_done_hw(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = (1 << (handle & 0x1f));

	arc_write_aux(DMA_S_DONESTATD_CLR_AUX(d), b);
}

static inline uint32_t dma_arc_hs_get_done_hw(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = handle & 0x1f;

	uint32_t volatile state = (arc_read_aux(DMA_S_DONESTATD_AUX(d & 0x7))) >> b;

	return state & 0x1;
}

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
	struct k_spinlock hw_lock; /* Per-channel hardware access lock */
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
	struct k_thread completion_thread;
	k_tid_t completion_thread_id;
	bool completion_thread_running;

	K_KERNEL_STACK_MEMBER(completion_stack, 1024);
};

static int dma_arc_hs_config(const struct device *dev, uint32_t channel, struct dma_config *config)
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

	/* Implicit channel allocation - allocate if not already in use */
	if (!chan->in_use) {
		chan->in_use = true;
		/* Update atomic bitmap for consistency with DMA framework */
		atomic_set_bit(data->channels_atomic, channel);
		LOG_INF("Implicitly allocated channel %u", channel);
	} else {
		LOG_INF("Channel %u already allocated", channel);
	}

	chan->config = *config;
	chan->callback = config->dma_callback;
	chan->callback_arg = config->user_data;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Configured channel %u", channel);
	return 0;
}

static int dma_arc_hs_start(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	struct dma_block_config *block;
	uint32_t attr;
	k_spinlock_key_t key, hw_key;

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

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

	LOG_INF("Starting HW transfer: ch=%u, src=0x%x, dst=0x%x, size=%u", channel,
		(uint32_t)block->source_address, (uint32_t)block->dest_address, block->block_size);

	dma_arc_hs_start_hw(channel, (const void *)block->source_address,
			    (void *)block->dest_address, block->block_size, attr);

	chan->handle = dma_arc_hs_get_handle_hw();
	chan->active = true;

	LOG_INF("HW transfer started: ch=%u, handle=%u", channel, chan->handle);

	k_spin_unlock(&chan->hw_lock, hw_key);
	k_spin_unlock(&data->lock, key);

	LOG_DBG("Started DMA transfer on channel %u, handle %u", channel, chan->handle);
	return 0;
}

static int dma_arc_hs_stop(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key, hw_key;

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

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

	chan->active = false;
	dma_arc_hs_clear_done_hw(chan->handle);

	k_spin_unlock(&chan->hw_lock, hw_key);
	k_spin_unlock(&data->lock, key);

	LOG_DBG("Stopped DMA transfer on channel %u", channel);
	return 0;
}

static void dma_arc_hs_check_completion(const struct device *dev, uint32_t channel)
{
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	uint32_t done_status;
	k_spinlock_key_t key, hw_key;

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (!chan->in_use || !chan->active) {
		k_spin_unlock(&data->lock, key);
		return;
	}

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

	done_status = dma_arc_hs_get_done_hw(chan->handle);

	if (done_status != 0) {
		LOG_INF("Channel %u transfer completed, clearing done status", channel);
		chan->active = false;
		dma_arc_hs_clear_done_hw(chan->handle);

		if (chan->callback) {
			LOG_INF("Calling callback for channel %u", channel);
			chan->callback(dev, chan->callback_arg, channel, 0);
		}
	}

	k_spin_unlock(&chan->hw_lock, hw_key);
	k_spin_unlock(&data->lock, key);
}

static int dma_arc_hs_get_status(const struct device *dev, uint32_t channel,
				 struct dma_status *stat)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	uint32_t done_status;
	k_spinlock_key_t key, hw_key;

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
		/* Lock hardware access for this channel */
		hw_key = k_spin_lock(&chan->hw_lock);

		done_status = dma_arc_hs_get_done_hw(chan->handle);
		LOG_INF("Channel %u status check: handle=%u, done_status=%u", channel, chan->handle,
			done_status);

		if (done_status == 0) {
			stat->busy = true;
			if (chan->config.head_block) {
				stat->pending_length = chan->config.head_block->block_size;
			}
			LOG_INF("Channel %u still busy, pending=%u", channel, stat->pending_length);
		} else {
			LOG_INF("Channel %u transfer completed, clearing done status", channel);
			chan->active = false;
			dma_arc_hs_clear_done_hw(chan->handle);

			if (chan->callback) {
				LOG_INF("Calling callback for channel %u", channel);
				chan->callback(dev, chan->callback_arg, channel, 0);
			}
		}

		k_spin_unlock(&chan->hw_lock, hw_key);
	} else {
		LOG_INF("Channel %u not active", channel);
	}

	k_spin_unlock(&data->lock, key);
	return 0;
}

static bool dma_arc_hs_chan_filter(const struct device *dev, int channel, void *filter_param)
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
		/* Update atomic bitmap for consistency with DMA framework */
		atomic_set_bit(data->channels_atomic, channel);
		result = true;
	}

	k_spin_unlock(&data->lock, key);

	if (result) {
		LOG_DBG("Allocated channel %d", channel);
	}

	return result;
}

static void dma_arc_hs_chan_release(const struct device *dev, uint32_t channel)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key, hw_key;

	if (channel >= dev_config->channels) {
		return;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (chan->active) {
		/* Lock hardware access for this channel */
		hw_key = k_spin_lock(&chan->hw_lock);

		chan->active = false;
		dma_arc_hs_clear_done_hw(chan->handle);

		k_spin_unlock(&chan->hw_lock, hw_key);
	}

	chan->in_use = false;
	/* Update atomic bitmap for consistency with DMA framework */
	atomic_clear_bit(data->channels_atomic, channel);
	memset(&chan->config, 0, sizeof(chan->config));
	chan->callback = NULL;
	chan->callback_arg = NULL;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Released channel %u", channel);
}

static int dma_arc_hs_get_attribute(const struct device *dev, uint32_t type, uint32_t *value)
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

static void dma_arc_hs_completion_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = (const struct device *)arg1;
	const struct arc_dma_config *config = dev->config;
	struct arc_dma_data *data = dev->data;
	int i;

	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	LOG_DBG("DMA completion thread started");

	while (data->completion_thread_running) {
		/* Check all channels for completion */
		for (i = 0; i < config->channels; i++) {
			dma_arc_hs_check_completion(dev, i);
		}

		/* Sleep for 10ms between checks - similar to old polling interval */
		k_sleep(K_MSEC(10));
	}

	LOG_DBG("DMA completion thread stopped");
}

static const struct dma_driver_api dma_arc_hs_api = {
	.config = dma_arc_hs_config,
	.start = dma_arc_hs_start,
	.stop = dma_arc_hs_stop,
	.get_status = dma_arc_hs_get_status,
	.chan_filter = dma_arc_hs_chan_filter,
	.chan_release = dma_arc_hs_chan_release,
	.get_attribute = dma_arc_hs_get_attribute,
};

static int dma_arc_hs_init(const struct device *dev)
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
		/* Spinlocks are zero-initialized by default in Zephyr */
	}

	dma_arc_hs_config_hw();

	for (i = 0; i < config->channels; i++) {
		dma_arc_hs_init_channel_hw(i, 0, config->descriptors - 1);
	}

	/* Start completion checking thread */
	data->completion_thread_running = true;
	data->completion_thread_id = k_thread_create(
		&data->completion_thread, data->completion_stack,
		K_KERNEL_STACK_SIZEOF(data->completion_stack), dma_arc_hs_completion_thread,
		(void *)dev, NULL, NULL, K_PRIO_COOP(7), /* High priority thread */
		0, K_NO_WAIT);

	k_thread_name_set(data->completion_thread_id, "arc_dma_completion");

	LOG_INF("ARC DMA initialized successfully with completion thread");
	return 0;
}

static int dma_arc_hs_cleanup(const struct device *dev)
{
	struct arc_dma_data *data = dev->data;

	/* Stop completion thread */
	if (data->completion_thread_running) {
		data->completion_thread_running = false;
		k_thread_join(data->completion_thread_id, K_FOREVER);
		LOG_DBG("DMA completion thread stopped and joined");
	}

	return 0;
}

__maybe_unused static int dma_arc_hs_cleanup_wrapper(const struct device *dev)
{
	return dma_arc_hs_cleanup(dev);
}

#define ARC_DMA_INIT(inst)                                                                         \
	static const struct arc_dma_config arc_dma_config_##inst = {                               \
		.base = DT_INST_REG_ADDR(inst),                                                    \
		.channels = DT_INST_PROP(inst, dma_channels),                                      \
		.descriptors = DT_INST_PROP_OR(inst, dma_descriptors, 32),                         \
		.max_burst_size = DT_INST_PROP_OR(inst, max_burst_size, 4),                        \
		.max_pending_transactions = DT_INST_PROP_OR(inst, max_pending_transactions, 4),    \
		.buffer_size = DT_INST_PROP_OR(inst, buffer_size, 16),                             \
		.coherency_support = DT_INST_PROP(inst, coherency_support),                        \
	};                                                                                         \
                                                                                                   \
	static struct arc_dma_data arc_dma_data_##inst;                                            \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dma_arc_hs_init, dma_arc_hs_cleanup_wrapper,                   \
			      &arc_dma_data_##inst, &arc_dma_config_##inst, POST_KERNEL,           \
			      CONFIG_DMA_INIT_PRIORITY, &dma_arc_hs_api);

DT_INST_FOREACH_STATUS_OKAY(ARC_DMA_INIT)
