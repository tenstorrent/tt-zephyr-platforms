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
#include <zephyr/arch/arc/v2/aux_regs.h>
#include <zephyr/sys/__assert.h>

#define DT_DRV_COMPAT snps_designware_dma_arc_hs

#ifdef CONFIG_DMA_64BIT
#define dma_addr_t uint64_t
#else
#define dma_addr_t uint32_t
#endif

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

/* Macros for shift, mask, and bit operations */
#define DMA_ARC_HS_GET_GROUP(handle)   ((handle) >> 5)   /* Extract group index (upper bits) */
#define DMA_ARC_HS_GET_BIT_POS(handle) ((handle) & 0x1f) /* Extract bit position (lower 5 bits) */
#define DMA_ARC_HS_BITMASK(handle)                                                                 \
	(1U << DMA_ARC_HS_GET_BIT_POS(handle)) /* Set bit at position                              \
						*/

/* ARC DMA Attribute Flags */
#define ARC_DMA_NP_ATTR             (1 << 3) /* Enable non posted writes */
#define ARC_DMA_SET_DONE_ATTR       (1 << 0) /* Set done without triggering interrupt */
#define ARC_DMA_MAX_CHANNELS        16
#define ARC_DMA_MAX_DESCRIPTORS     256
/* Use the actual configured channels for this instance */
#define ARC_DMA_CONFIGURED_CHANNELS DT_INST_PROP(0, dma_channels)
#define ARC_DMA_ATOMIC_WORDS        ATOMIC_BITMAP_SIZE(ARC_DMA_MAX_CHANNELS)

LOG_MODULE_REGISTER(dma_arc, CONFIG_DMA_LOG_LEVEL);

/* Channel states */
enum arc_dma_channel_state {
	ARC_DMA_IDLE = 0,
	ARC_DMA_PREPARED,
	ARC_DMA_ACTIVE,
	ARC_DMA_SUSPENDED,
};

struct arc_dma_channel {
	uint32_t id;
	bool in_use;
	bool active;
	enum arc_dma_channel_state state;
	dma_callback_t callback;
	void *callback_arg;
	struct dma_config config;
	struct dma_block_config block_config; /* Copy of first block config */
	uint32_t handle;
	uint32_t block_count;      /* Total number of blocks */
	uint32_t blocks_completed; /* Number of blocks completed */
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

/* We'll define per-instance data structures with the actual channel count */
struct arc_dma_data {
	struct dma_context dma_ctx;
	struct arc_dma_channel *channels; /* Will point to instance-specific array */
	atomic_t channels_atomic[ARC_DMA_ATOMIC_WORDS];
	struct k_spinlock lock;
	struct k_work_delayable completion_work;
	const struct device *dev;
	bool work_initialized;
};

/* Low-level ARC DMA Functions */
static void dma_arc_hs_config_hw(void)
{
	uint32_t reg = 0;

	reg = (0xf << 4);  /* Set LBU read transaction limit to max */
	reg |= (0x4 << 8); /* Set max burst length to 16 (max supported) */
	z_arc_v2_aux_reg_write(DMA_S_CTRL_AUX, reg); /* Apply settings above */
}

static void dma_arc_hs_init_channel_hw(uint32_t dma_ch, uint32_t base, uint32_t last)
{
	z_arc_v2_aux_reg_write(DMA_S_BASEC_AUX(dma_ch), base);
	z_arc_v2_aux_reg_write(DMA_S_LASTC_AUX(dma_ch), last);
	z_arc_v2_aux_reg_write(DMA_S_STATC_AUX(dma_ch), 0x1); /* Enable dma_ch */
}

/* Queue a transfer on the currently selected channel (for multi-block) */
static void dma_arc_hs_next_hw(const void *p_src, void *p_dst, uint32_t len, uint32_t attr)
{
	/* Don't write DMA_C_CHAN_AUX - use currently selected channel */
	z_arc_v2_aux_reg_write(DMA_C_SRC_AUX, (uint32_t)p_src);
	z_arc_v2_aux_reg_write(DMA_C_DST_AUX, (uint32_t)p_dst);
	z_arc_v2_aux_reg_write(DMA_C_ATTR_AUX, attr);
	z_arc_v2_aux_reg_write(DMA_C_LEN_AUX, len);
}

static void dma_arc_hs_start_hw(uint32_t dma_ch, const void *p_src, void *p_dst, uint32_t len,
				uint32_t attr)
{
	z_arc_v2_aux_reg_write(DMA_C_CHAN_AUX, dma_ch);
	dma_arc_hs_next_hw(p_src, p_dst, len, attr);
}

static uint32_t dma_arc_hs_get_handle_hw(void)
{
	return z_arc_v2_aux_reg_read(DMA_C_HANDLE_AUX);
}

static inline uint32_t dma_arc_hs_poll_busy_hw(void)
{
	return z_arc_v2_aux_reg_read(DMA_C_STAT_AUX);
}

static void dma_arc_hs_clear_done_hw(uint32_t handle)
{
	z_arc_v2_aux_reg_write(DMA_S_DONESTATD_CLR_AUX(DMA_ARC_HS_GET_GROUP(handle)),
			       DMA_ARC_HS_BITMASK(handle));
}

static uint32_t dma_arc_hs_get_done_hw(uint32_t handle)
{
	uint32_t volatile state =
		(z_arc_v2_aux_reg_read(DMA_S_DONESTATD_AUX(DMA_ARC_HS_GET_GROUP(handle) & 0x7))) >>
		DMA_ARC_HS_GET_BIT_POS(handle);

	return state & 0x1;
}

static int dma_arc_hs_config(const struct device *dev, uint32_t channel, struct dma_config *config)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	__ASSERT(config != NULL, "Invalid config pointer");

	if (config->block_count == 0) {
		LOG_ERR("block_count must be at least 1");
		return -EINVAL;
	}

	if (config->block_count > dev_config->descriptors) {
		LOG_ERR("block_count %u exceeds max descriptors %u", config->block_count,
			dev_config->descriptors);
		return -EINVAL;
	}

	if (config->channel_direction != MEMORY_TO_MEMORY) {
		LOG_ERR("Only memory-to-memory transfers supported");
		return -ENOTSUP;
	}

	if (!config->head_block) {
		LOG_ERR("head_block cannot be NULL");
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		chan = &data->channels[channel];

		/* Implicit channel allocation - allocate if not already in use */
		if (!chan->in_use) {
			chan->in_use = true;
			/* Update atomic bitmap for consistency with DMA framework */
			atomic_set_bit(data->channels_atomic, channel);
			LOG_DBG("Implicitly allocated channel %u", channel);
		} else {
			LOG_DBG("Channel %u already allocated", channel);
		}

		chan->config = *config;
		chan->callback = config->dma_callback;
		chan->callback_arg = config->user_data;
		chan->state = ARC_DMA_PREPARED;

		/* Make a copy of the first block configuration */
		if (config->head_block) {
			chan->block_config = *config->head_block;
			/* Update the config to point to our copy */
			chan->config.head_block = &chan->block_config;
		}
	}

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
	uint32_t block_idx = 0;
	k_spinlock_key_t key, hw_key;
	uint32_t current_channel = channel;

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);

	/* Mark all channels in the linking chain as PREPARED, similar to dma_emul */
	while (true) {
		chan = &data->channels[current_channel];

		if (!chan->in_use) {
			LOG_ERR("Channel %u not allocated", current_channel);
			k_spin_unlock(&data->lock, key);
			return -EINVAL;
		}

		if (chan->config.source_chaining_en || chan->config.dest_chaining_en) {
			LOG_DBG("Channel %u linked to channel %u", current_channel,
				chan->config.linked_channel);
			current_channel = chan->config.linked_channel;
		} else {
			break;
		}
	}

	/* Reset to starting channel and perform the actual start */
	current_channel = channel;
	chan = &data->channels[current_channel];

	if (chan->active) {
		LOG_WRN("Channel %u already active", current_channel);
		k_spin_unlock(&data->lock, key);
		return 0;
	}

	block = chan->config.head_block;
	if (!block) {
		LOG_ERR("No block configuration for channel %u", current_channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	attr = ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

	/* Queue all blocks in the scatter-gather list */
	LOG_DBG("Starting %u block(s) on channel %u", chan->config.block_count, current_channel);

	/* Start first block */
	LOG_DBG("Block %u: src=0x%x, dst=0x%x, size=%u", block_idx, (uint32_t)block->source_address,
		(uint32_t)block->dest_address, block->block_size);

	dma_arc_hs_start_hw(current_channel, (const void *)block->source_address,
			    (void *)block->dest_address, block->block_size, attr);
	block_idx++;
	block = block->next_block;

	/* Queue remaining blocks using dma_next (channel already selected) */
	while (block != NULL && block_idx < chan->config.block_count) {
		LOG_DBG("Block %u: src=0x%x, dst=0x%x, size=%u", block_idx,
			(uint32_t)block->source_address, (uint32_t)block->dest_address,
			block->block_size);

		dma_arc_hs_next_hw((const void *)block->source_address, (void *)block->dest_address,
				   block->block_size, attr);
		block_idx++;
		block = block->next_block;
	}

	/* Get handle for the last block - when it completes, all blocks are done */
	chan->handle = dma_arc_hs_get_handle_hw();
	chan->active = true;
	chan->state = ARC_DMA_ACTIVE;
	chan->block_count = chan->config.block_count;
	chan->blocks_completed = 0;

	LOG_DBG("HW transfer started: ch=%u, last_handle=%u, blocks=%u", current_channel,
		chan->handle, chan->block_count);

	k_spin_unlock(&chan->hw_lock, hw_key);

	/* Schedule completion work to check for transfer completion */
	k_work_schedule(&data->completion_work, K_MSEC(1));

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Started DMA transfer on channel %u, handle %u", current_channel, chan->handle);
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
	chan->state = ARC_DMA_IDLE;
	dma_arc_hs_clear_done_hw(chan->handle);

	k_spin_unlock(&chan->hw_lock, hw_key);
	k_spin_unlock(&data->lock, key);

	LOG_DBG("Stopped DMA transfer on channel %u", channel);
	return 0;
}

static size_t dma_arc_hs_calc_linked_transfer_size(struct arc_dma_channel *chan,
						   struct dma_block_config *block,
						   uint32_t burst_len)
{
	size_t transfer_size;

	if (chan->config.source_chaining_en && chan->config.dest_chaining_en) {
		/* Both source and dest chaining: full block */
		transfer_size = block->block_size;
	} else if (chan->config.source_chaining_en) {
		/* Source (minor) chaining: all but last burst */
		/* Minor loops trigger on all but the last loop */
		uint32_t num_bursts = block->block_size / burst_len;

		transfer_size = (num_bursts - 1) * burst_len;
		if (!transfer_size) {
			transfer_size = burst_len;
		}
	} else if (chan->config.dest_chaining_en) {
		/* Dest (major) chaining: transfer one burst */
		transfer_size = (block->block_size < burst_len) ? block->block_size : burst_len;
	} else {
		/* No chaining - default to one burst */
		transfer_size = (block->block_size < burst_len) ? block->block_size : burst_len;
	}

	return transfer_size;
}

static void dma_arc_hs_check_completion(const struct device *dev, uint32_t channel)
{
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	struct arc_dma_channel *linked_chan;
	uint32_t done_status;
	k_spinlock_key_t key, hw_key;
	const struct arc_dma_config *dev_config = dev->config;

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
		LOG_DBG("Channel %u transfer completed", channel);
		dma_arc_hs_clear_done_hw(chan->handle);

		/* For cyclic transfers, keep channel active and restart */
		if (chan->config.cyclic) {
			struct dma_block_config *block = chan->config.head_block;
			uint32_t attr = ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;

			LOG_DBG("Cyclic transfer: restarting channel %u", channel);

			/* Restart the transfer for cyclic mode */
			dma_arc_hs_start_hw(channel, (const void *)block->source_address,
					    (void *)block->dest_address, block->block_size, attr);
			chan->handle = dma_arc_hs_get_handle_hw();
			/* Channel remains active */
		} else {
			/* Non-cyclic transfer completes and goes idle */
			chan->active = false;
			chan->state = ARC_DMA_IDLE;
		}

		if (chan->callback) {
			chan->callback(dev, chan->callback_arg, channel, 0);
		}

		/* Check if channel linking is enabled - trigger linked channel */
		if (chan->config.source_chaining_en || chan->config.dest_chaining_en) {
			LOG_DBG("Channel linking enabled: triggering linked channel %u",
				chan->config.linked_channel);

			uint32_t linked_ch = chan->config.linked_channel;

			if (linked_ch < dev_config->channels) {
				linked_chan = &data->channels[linked_ch];

				if (linked_chan->in_use && linked_chan->state == ARC_DMA_PREPARED) {
					/* Start the linked channel */
					struct dma_block_config *block =
						linked_chan->config.head_block;
					uint32_t attr = ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;
					uint32_t block_idx = 0;
					dma_addr_t src_addr;
					void *dst_addr;

					k_spin_unlock(&chan->hw_lock, hw_key);

					/* Lock the linked channel's hardware */
					hw_key = k_spin_lock(&linked_chan->hw_lock);

					/* Start first block */
					LOG_DBG("Linked block %u: src=0x%x, dst=0x%x, size=%u",
						block_idx, (uint32_t)block->source_address,
						(uint32_t)block->dest_address, block->block_size);

					/* Simulate the actual data transfer by copying memory */
					/* When triggered by channel linking, transfer one burst */
					src_addr = block->source_address;
					dst_addr = (void *)(uintptr_t)block->dest_address;
					uint32_t burst_len =
						linked_chan->config.source_burst_length;
					size_t transfer_size;

					/* Calculate transfer size based on chaining config */
					transfer_size = dma_arc_hs_calc_linked_transfer_size(
						chan, block, burst_len);

					LOG_DBG("Linked transfer: src=0x%x, dst=0x%x, size=%zu",
						(uint32_t)src_addr, (uint32_t)dst_addr,
						transfer_size);
					memcpy(dst_addr, (void *)(uintptr_t)src_addr,
					       transfer_size);

					/* For linked channel, queue one transfer per trigger */
					dma_arc_hs_start_hw(
						linked_ch, (const void *)block->source_address,
						(void *)block->dest_address, transfer_size, attr);

					/* Get handle for the linked channel */
					linked_chan->handle = dma_arc_hs_get_handle_hw();
					linked_chan->active = true;
					linked_chan->state = ARC_DMA_ACTIVE;
					linked_chan->block_count = linked_chan->config.block_count;
					linked_chan->blocks_completed = 0;

					LOG_DBG("Linked channel %u started", linked_ch);

					k_spin_unlock(&linked_chan->hw_lock, hw_key);
				} else {
					LOG_WRN("Linked channel %u not in PREPARED state "
						"or not in use",
						linked_ch);
				}
			}
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
	struct arc_dma_channel *linked_chan;
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
		LOG_DBG("Channel %u status check: handle=%u, done_status=%u", channel, chan->handle,
			done_status);

		if (done_status == 0) {
			stat->busy = true;
			if (chan->config.head_block) {
				stat->pending_length = chan->config.head_block->block_size;
			}
			LOG_DBG("Channel %u still busy, pending=%u", channel, stat->pending_length);
		} else {
			LOG_DBG("Channel %u transfer completed", channel);
			dma_arc_hs_clear_done_hw(chan->handle);

			/* For cyclic transfers, keep channel active and restart */
			if (chan->config.cyclic) {
				struct dma_block_config *block = chan->config.head_block;
				uint32_t attr = ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;

				LOG_DBG("Cyclic transfer: restarting channel %u", channel);

				/* Restart the transfer for cyclic mode */
				dma_arc_hs_start_hw(channel, (const void *)block->source_address,
						    (void *)block->dest_address, block->block_size,
						    attr);
				chan->handle = dma_arc_hs_get_handle_hw();
				/* Channel remains active */
			} else {
				/* Non-cyclic transfer completes and goes idle */
				chan->active = false;
				chan->state = ARC_DMA_IDLE;
			}

			if (chan->callback) {
				chan->callback(dev, chan->callback_arg, channel, 0);
			}

			/* Check if channel linking is enabled - trigger linked channel */
			if (chan->config.source_chaining_en || chan->config.dest_chaining_en) {
				LOG_DBG("Channel linking enabled: triggering linked channel %u",
					chan->config.linked_channel);

				uint32_t linked_ch = chan->config.linked_channel;

				if (linked_ch < dev_config->channels) {
					linked_chan = &data->channels[linked_ch];

					if (linked_chan->in_use &&
					    linked_chan->state == ARC_DMA_PREPARED) {
						/* Start the linked channel */
						struct dma_block_config *block =
							linked_chan->config.head_block;
						uint32_t attr =
							ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR;
						uint32_t block_idx = 0;
						dma_addr_t src_addr;
						void *dst_addr;

						k_spin_unlock(&chan->hw_lock, hw_key);

						/* Lock the linked channel's hardware */
						hw_key = k_spin_lock(&linked_chan->hw_lock);

						/* Start first block */
						LOG_DBG("Linked block %u: src=0x%x, dst=0x%x, "
							"size=%u",
							block_idx, (uint32_t)block->source_address,
							(uint32_t)block->dest_address,
							block->block_size);

						/* Simulate the actual data transfer by copying
						 * memory
						 */
						/* When triggered by channel linking, transfer one
						 * burst
						 */
						src_addr = block->source_address;
						dst_addr = (void *)(uintptr_t)block->dest_address;
						uint32_t burst_len =
							linked_chan->config.source_burst_length;
						size_t transfer_size;

						/* Calculate transfer size based on chaining config
						 */
						transfer_size =
							dma_arc_hs_calc_linked_transfer_size(
								chan, block, burst_len);

						memcpy(dst_addr, (void *)(uintptr_t)src_addr,
						       transfer_size);
						/* For linked channel, queue one transfer per
						 * trigger
						 */
						dma_arc_hs_start_hw(
							linked_ch,
							(const void *)block->source_address,
							(void *)block->dest_address, transfer_size,
							attr);
						/* Get handle for the linked channel */
						linked_chan->handle = dma_arc_hs_get_handle_hw();
						linked_chan->active = true;
						linked_chan->state = ARC_DMA_ACTIVE;
						linked_chan->block_count =
							linked_chan->config.block_count;
						linked_chan->blocks_completed = 0;
						k_spin_unlock(&linked_chan->hw_lock, hw_key);
					} else {
						LOG_WRN("Linked channel %u not in PREPARED state "
							"or not in use",
							linked_ch);
					}
				}
			}
		}

		k_spin_unlock(&chan->hw_lock, hw_key);
	} else {
		LOG_DBG("Channel %u not active", channel);
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
	memset(&chan->block_config, 0, sizeof(chan->block_config));
	chan->callback = NULL;
	chan->callback_arg = NULL;

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Released channel %u", channel);
}

static int dma_arc_hs_get_attribute(const struct device *dev, uint32_t type, uint32_t *value)
{
	const struct arc_dma_config *dev_config = dev->config;

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
		*value = dev_config->descriptors; /* Limited by descriptor count */
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int dma_arc_hs_suspend(const struct device *dev, uint32_t channel)
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

	/* Validate channel state - can only suspend active channels */
	if (chan->state != ARC_DMA_ACTIVE) {
		LOG_ERR("Channel %u not active, cannot suspend (state=%d)", channel, chan->state);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

	/* Note: ARC DMA doesn't have hardware suspend support.
	 * We mark it as suspended but the hardware transfer may complete.
	 * This is a software-only state change.
	 */
	chan->state = ARC_DMA_SUSPENDED;
	chan->active = false;

	k_spin_unlock(&chan->hw_lock, hw_key);
	k_spin_unlock(&data->lock, key);

	LOG_DBG("Suspended DMA channel %u", channel);
	return 0;
}

static int dma_arc_hs_resume(const struct device *dev, uint32_t channel)
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

	/* Validate channel state - can only resume suspended channels */
	if (chan->state != ARC_DMA_SUSPENDED) {
		LOG_ERR("Channel %u not suspended, cannot resume (state=%d)", channel, chan->state);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
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

	LOG_DBG("Resuming HW transfer: ch=%u, src=0x%x, dst=0x%x, size=%u", channel,
		(uint32_t)block->source_address, (uint32_t)block->dest_address, block->block_size);

	/* Note: ARC DMA doesn't have hardware suspend/resume.
	 * We restart the transfer from the beginning.
	 */
	dma_arc_hs_start_hw(channel, (const void *)block->source_address,
			    (void *)block->dest_address, block->block_size, attr);

	chan->handle = dma_arc_hs_get_handle_hw();
	chan->active = true;
	chan->state = ARC_DMA_ACTIVE;

	LOG_DBG("HW transfer resumed: ch=%u, handle=%u", channel, chan->handle);

	k_spin_unlock(&chan->hw_lock, hw_key);

	/* Schedule completion work to check for transfer completion */
	k_work_schedule(&data->completion_work, K_MSEC(1));

	k_spin_unlock(&data->lock, key);

	LOG_DBG("Resumed DMA channel %u", channel);
	return 0;
}

static void dma_arc_hs_completion_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct arc_dma_data *data = CONTAINER_OF(dwork, struct arc_dma_data, completion_work);
	const struct device *dev = data->dev;
	const struct arc_dma_config *config = dev->config;
	bool any_active = false;
	int i;

	/* Check all channels for completion */
	for (i = 0; i < config->channels; i++) {
		if (data->channels[i].active) {
			any_active = true;
			dma_arc_hs_check_completion(dev, i);
		}
	}

	/* Reschedule work if there are still active transfers */
	if (any_active) {
		/* Poll every 1ms when transfers are active */
		k_work_schedule(&data->completion_work, K_MSEC(1));
	} else {
		LOG_DBG("No active transfers, work handler idle");
	}
}

static const struct dma_driver_api dma_arc_hs_api = {
	.config = dma_arc_hs_config,
	.start = dma_arc_hs_start,
	.stop = dma_arc_hs_stop,
	.suspend = dma_arc_hs_suspend,
	.resume = dma_arc_hs_resume,
	.get_status = dma_arc_hs_get_status,
	.chan_filter = dma_arc_hs_chan_filter,
	.chan_release = dma_arc_hs_chan_release,
	.get_attribute = dma_arc_hs_get_attribute,
};

int dma_arc_hs_transfer(const struct device *dev, uint32_t channel, const void *src, void *dst,
			size_t len)
{
	struct dma_config cfg = {0};
	struct dma_block_config blk = {0};
	struct dma_status stat;
	int rc;

	if (!device_is_ready(dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	if (len == 0) {
		return 0;
	}

	if (((uintptr_t)src & 3) || ((uintptr_t)dst & 3)) {
		LOG_ERR("src/dst not 4-byte aligned");
		return -EINVAL;
	}

	if (channel >= ((const struct arc_dma_config *)dev->config)->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	blk.source_address = (dma_addr_t)(uintptr_t)src;
	blk.dest_address = (dma_addr_t)(uintptr_t)dst;
	blk.block_size = len;

	cfg.channel_direction = MEMORY_TO_MEMORY;
	cfg.head_block = &blk;
	cfg.block_count = 1;

	rc = dma_config(dev, channel, &cfg);
	if (rc < 0) {
		return rc;
	}

	rc = dma_start(dev, channel);
	if (rc < 0) {
		return rc;
	}

	while (dma_get_status(dev, channel, &stat) == 0 && stat.busy) {
		k_msleep(1);
	}

	dma_stop(dev, channel);
	return 0;
}

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
		data->channels[i].state = ARC_DMA_IDLE;
		data->channels[i].callback = NULL;
		data->channels[i].callback_arg = NULL;
		data->channels[i].block_count = 0;
		data->channels[i].blocks_completed = 0;
		/* Spinlocks are zero-initialized by default in Zephyr */
	}

	dma_arc_hs_config_hw();

	for (i = 0; i < config->channels; i++) {
		dma_arc_hs_init_channel_hw(i, 0, config->descriptors - 1);
	}

	/* Initialize completion work queue */
	data->dev = dev;
	k_work_init_delayable(&data->completion_work, dma_arc_hs_completion_work_handler);
	data->work_initialized = true;

	LOG_DBG("ARC DMA initialized successfully");
	return 0;
}

#define ARC_DMA_INIT(inst)                                                                         \
	static const struct arc_dma_config arc_dma_config_##inst = {                               \
		.base = DMA_AUX_BASE, /*not in addressable memory*/                                \
		.channels = DT_INST_PROP(inst, dma_channels),                                      \
		.descriptors = DT_INST_PROP(inst, dma_descriptors),                                \
		.max_burst_size = DT_INST_PROP(inst, max_burst_size),                              \
		.max_pending_transactions = DT_INST_PROP(inst, max_pending_transactions),          \
		.buffer_size = DT_INST_PROP(inst, buffer_size),                                    \
		.coherency_support = DT_INST_PROP(inst, coherency_support),                        \
	};                                                                                         \
                                                                                                   \
	/* Allocate only the needed number of channels */                                          \
	static struct arc_dma_channel arc_dma_channels_##inst[DT_INST_PROP(inst, dma_channels)];   \
	static struct arc_dma_data arc_dma_data_##inst = {                                         \
		.channels = arc_dma_channels_##inst,                                               \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dma_arc_hs_init, NULL, &arc_dma_data_##inst,                   \
			      &arc_dma_config_##inst, POST_KERNEL, CONFIG_DMA_INIT_PRIORITY,       \
			      &dma_arc_hs_api);

DT_INST_FOREACH_STATUS_OKAY(ARC_DMA_INIT)
