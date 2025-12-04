/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_arc_hs.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>
#include <zephyr/irq.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/arch/arc/v2/aux_regs.h>
#include <zephyr/arch/common/ffs.h>
#include <zephyr/sys/__assert.h>

#define DT_DRV_COMPAT snps_designware_dma_arc_hs

#ifdef CONFIG_DMA_64BIT
#define dma_addr_t uint64_t
#else
#define dma_addr_t uint32_t
#endif

#define DMA_AUX_BASE           (0xd00)
#define DMA_C_CTRL_AUX         (0xd00 + 0x0)
#define DMA_C_CHAN_AUX         (0xd00 + 0x1)
#define DMA_C_SRC_AUX          (0xd00 + 0x2)
#define DMA_C_SRC_HI_AUX       (0xd00 + 0x3)
#define DMA_C_DST_AUX          (0xd00 + 0x4)
#define DMA_C_DST_HI_AUX       (0xd00 + 0x5)
#define DMA_C_ATTR_AUX         (0xd00 + 0x6)
#define DMA_C_LEN_AUX          (0xd00 + 0x7)
#define DMA_C_HANDLE_AUX       (0xd00 + 0x8)
#define DMA_C_STAT_AUX         (0xd00 + 0xc)
#define DMA_C_INTSTAT_AUX      (0xd00 + 0xd)
#define DMA_C_INTSTAT_CLR_AUX  (0xd00 + 0xe)
/* DMA_C_INTSTAT_AUX bit definitions */
#define DMA_C_INTSTAT_DONE     (1 << 0) /* D: Transfer complete */
#define DMA_C_INTSTAT_BUS_ERR  (1 << 1) /* B: Bus error */
#define DMA_C_INTSTAT_OVERFLOW (1 << 2) /* O: Channel overflow */

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
#define ARC_DMA_INT_EN_ATTR         (1 << 1) /* Enable interrupt on completion */
#define ARC_DMA_MAX_CHANNELS        16
#define ARC_DMA_MAX_DESCRIPTORS     256
/* Use the actual configured channels for this instance */
#define ARC_DMA_CONFIGURED_CHANNELS DT_INST_PROP(0, dma_channels)
#define ARC_DMA_ATOMIC_WORDS        ATOMIC_BITMAP_SIZE(ARC_DMA_MAX_CHANNELS)

LOG_MODULE_REGISTER(dma_arc, CONFIG_DMA_LOG_LEVEL);

enum arc_dma_channel_state {
	ARC_DMA_FREE = 0,  /* Channel not allocated */
	ARC_DMA_IDLE,      /* Allocated but stopped */
	ARC_DMA_PREPARED,  /* Configured, ready to start */
	ARC_DMA_ACTIVE,    /* Transfer in progress */
	ARC_DMA_SUSPENDED, /* Transfer suspended */
};

struct arc_dma_channel {
	enum arc_dma_channel_state state;
	dma_callback_t callback;
	void *callback_arg;
	struct dma_config config;
	struct dma_block_config block_config; /* Copy of first block config */
	uint32_t handle;
	uint32_t block_count;      /* Total number of blocks */
	struct k_spinlock hw_lock; /* Per-channel hardware access lock */
};

struct arc_dma_config {
	uint32_t base;
	uint32_t channels;
	uint32_t descriptors;
	uint32_t max_burst_size;
	uint32_t max_pending_transactions;
	uint32_t buffer_size;
	uint32_t max_block_size;
	bool coherency_support;
	uint32_t buffer_address_alignment;
	void (*irq_config)(void);
};

/* We'll define per-instance data structures with the actual channel count */
struct arc_dma_data {
	struct dma_context dma_ctx;
	struct arc_dma_channel *channels; /* Will point to instance-specific array */
	atomic_t channels_atomic[ARC_DMA_ATOMIC_WORDS];
	struct k_spinlock lock;
	/* Static block array for splitting large transfers - sized by max descriptors */
	struct dma_block_config *transfer_blocks;
};

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

static void dma_arc_hs_start_hw(uint32_t dma_ch, const void *p_src, void *p_dst, uint32_t len,
				uint32_t attr)
{
	z_arc_v2_aux_reg_write(DMA_C_CHAN_AUX, dma_ch);
	z_arc_v2_aux_reg_write(DMA_C_SRC_AUX, (uint32_t)p_src);
	z_arc_v2_aux_reg_write(DMA_C_DST_AUX, (uint32_t)p_dst);
	z_arc_v2_aux_reg_write(DMA_C_ATTR_AUX, attr);
	z_arc_v2_aux_reg_write(DMA_C_LEN_AUX, len);
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
	__ASSERT(dev_config->descriptors <= 32, "Driver supports up to 32 descriptors (1 group)");

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

		/* Implicit channel allocation - allocate if not already allocated */
		if (chan->state == ARC_DMA_FREE) {
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

		if (chan->state == ARC_DMA_FREE) {
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

	if (chan->state == ARC_DMA_ACTIVE) {
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

	attr = ARC_DMA_INT_EN_ATTR | ARC_DMA_NP_ATTR;

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
	chan->state = ARC_DMA_ACTIVE;
	chan->block_count = chan->config.block_count;

	LOG_DBG("HW transfer started: ch=%u, last_handle=%u, blocks=%u", current_channel,
		chan->handle, chan->block_count);

	k_spin_unlock(&chan->hw_lock, hw_key);

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

	if (chan->state == ARC_DMA_FREE) {
		LOG_ERR("Channel %u not allocated", channel);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	if (chan->state != ARC_DMA_ACTIVE) {
		k_spin_unlock(&data->lock, key);
		return 0;
	}

	/* Lock hardware access for this channel */
	hw_key = k_spin_lock(&chan->hw_lock);

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

static void dma_arc_hs_trigger_linked_channel(const struct device *dev, struct arc_dma_data *data,
					      struct arc_dma_channel *triggering_chan,
					      uint32_t linked_ch_id)
{
	k_spinlock_key_t key;
	struct arc_dma_channel *linked_chan;

	LOG_DBG("Channel linking: trying to trigger channel %u", linked_ch_id);

	/* Re-acquire global lock only to safely read the linked channel state */
	key = k_spin_lock(&data->lock);
	linked_chan = &data->channels[linked_ch_id];

	if (linked_chan->state != ARC_DMA_PREPARED) {
		k_spin_unlock(&data->lock, key);
		LOG_WRN("Linked channel %u not ready (state=%d)", linked_ch_id, linked_chan->state);
		return;
	}

	/* Grab everything we need from the linked channel while protected */
	struct dma_block_config *block = linked_chan->config.head_block;
	uint32_t burst_len = linked_chan->config.source_burst_length;
	dma_addr_t src_addr = block->source_address;
	dma_addr_t dst_addr = block->dest_address;

	k_spin_unlock(&data->lock, key);

	/* Now safely start the linked transfer under its own hw_lock */
	k_spinlock_key_t linked_hw_key = k_spin_lock(&linked_chan->hw_lock);

	size_t transfer_size =
		dma_arc_hs_calc_linked_transfer_size(triggering_chan, block, burst_len);

	uint32_t attr = ARC_DMA_INT_EN_ATTR | ARC_DMA_NP_ATTR;

	dma_arc_hs_start_hw(linked_ch_id, (const void *)src_addr, (void *)(uintptr_t)dst_addr,
			    transfer_size, attr);

	linked_chan->handle = dma_arc_hs_get_handle_hw();
	linked_chan->state = ARC_DMA_ACTIVE;
	linked_chan->block_count = linked_chan->config.block_count;

	LOG_DBG("Linked channel %u started (size %zu)", linked_ch_id, transfer_size);

	k_spin_unlock(&linked_chan->hw_lock, linked_hw_key);
}

static int dma_arc_hs_get_status(const struct device *dev, uint32_t channel,
				 struct dma_status *stat)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct arc_dma_channel *chan;
	k_spinlock_key_t key;

	if (channel >= dev_config->channels) {
		return -EINVAL;
	}

	if (!stat) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	chan = &data->channels[channel];

	if (chan->state == ARC_DMA_FREE) {
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	stat->pending_length = 0;
	stat->dir = MEMORY_TO_MEMORY;
	stat->busy = false;

	if (chan->state == ARC_DMA_ACTIVE) {
		stat->busy = true;
		if (chan->config.head_block) {
			stat->pending_length = chan->config.head_block->block_size;
		}
	}

	k_spin_unlock(&data->lock, key);

	return 0;
}

static int dma_arc_hs_get_attribute(const struct device *dev, uint32_t type, uint32_t *value)
{
	const struct arc_dma_config *dev_config = dev->config;

	switch (type) {
	case DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT:
		*value = dev_config->buffer_address_alignment;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

int dma_arc_hs_transfer(const struct device *dev, uint32_t channel, const void *src, void *dst,
			size_t len, k_timeout_t timeout)
{
	const struct arc_dma_config *dev_config = dev->config;
	struct arc_dma_data *data = dev->data;
	struct dma_config cfg = {0};
	struct dma_status stat;
	k_timepoint_t end_time;
	int rc;
	size_t num_blocks;
	size_t max_block_size;

	if (!device_is_ready(dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	if (len == 0) {
		return 0;
	}

	/* Get alignment requirement from driver */
	uint32_t required_alignment;
	int ret = dma_get_attribute(dev, DMA_ATTR_BUFFER_ADDRESS_ALIGNMENT, &required_alignment);

	if (ret < 0) {
		LOG_ERR("Failed to get buffer address alignment: %d", ret);
		return ret;
	}

	/* Validate address alignment */
	uint32_t alignment_mask = required_alignment - 1;

	if (((uintptr_t)src & alignment_mask) || ((uintptr_t)dst & alignment_mask)) {
		LOG_ERR("src/dst not aligned to %u bytes", required_alignment);
		return -EINVAL;
	}

	if (channel >= dev_config->channels) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	/* Get maximum block size from device tree configuration */
	max_block_size = dev_config->max_block_size;

	/* Calculate number of blocks needed to split the transfer */
	num_blocks = (len + max_block_size - 1) / max_block_size;

	/* Check if we exceed the maximum number of descriptors */
	if (num_blocks > dev_config->descriptors) {
		LOG_ERR("Transfer size %zu requires %zu blocks but only %u descriptors available",
			len, num_blocks, dev_config->descriptors);
		return -EINVAL;
	}

	/* Use statically allocated transfer_blocks array (no malloc needed) */
	struct dma_block_config *blocks = data->transfer_blocks;

	/* Split the transfer into multiple blocks */
	size_t remaining = len;
	uintptr_t src_addr = (uintptr_t)src;
	uintptr_t dst_addr = (uintptr_t)dst;

	for (size_t i = 0; i < num_blocks; i++) {
		size_t block_len = (remaining > max_block_size) ? max_block_size : remaining;

		memset(&blocks[i], 0, sizeof(struct dma_block_config));
		blocks[i].source_address = (dma_addr_t)src_addr;
		blocks[i].dest_address = (dma_addr_t)dst_addr;
		blocks[i].block_size = block_len;
		blocks[i].next_block = (i < num_blocks - 1) ? &blocks[i + 1] : NULL;

		src_addr += block_len;
		dst_addr += block_len;
		remaining -= block_len;
	}

	if (num_blocks > 1) {
		LOG_DBG("Split %zu-byte transfer into %zu blocks of max %zu bytes", len, num_blocks,
			max_block_size);
	}

	cfg.channel_direction = MEMORY_TO_MEMORY;
	cfg.head_block = blocks;
	cfg.block_count = num_blocks;

	rc = dma_config(dev, channel, &cfg);
	if (rc < 0) {
		return rc;
	}

	rc = dma_start(dev, channel);
	if (rc < 0) {
		return rc;
	}

	end_time = sys_timepoint_calc(timeout);

	do {
		if (dma_get_status(dev, channel, &stat) == 0 && !stat.busy) {
			dma_stop(dev, channel);
			return 0;
		}

		/* Update timeout with remaining time */
		timeout = sys_timepoint_timeout(end_time);

		/* Busy wait for a short period */
		if (!K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			k_busy_wait(100);
		}
	} while (!K_TIMEOUT_EQ(timeout, K_NO_WAIT));

	/* Timeout expired */
	dma_stop(dev, channel);
	return -ETIMEDOUT;
}

static void dma_arc_hs_process_handle(const struct device *dev, struct arc_dma_data *data,
				      uint32_t handle)
{
	const struct arc_dma_config *config = dev->config;

	/* Find which channel this handle belongs to */
	for (int ch = 0; ch < config->channels; ch++) {
		struct arc_dma_channel *chan = &data->channels[ch];

		if (chan->state != ARC_DMA_ACTIVE || chan->handle != handle) {
			continue;
		}

		chan->state = ARC_DMA_IDLE;

		/* Dispatch callback */
		if (chan->callback != NULL) {
			chan->callback(dev, chan->callback_arg, ch, 0);
		}

		/* Handle channel linking */
		if (chan->config.source_chaining_en != 0U || chan->config.dest_chaining_en != 0U) {
			uint32_t linked_ch = chan->config.linked_channel;

			if (linked_ch < config->channels) {
				dma_arc_hs_trigger_linked_channel(dev, data, chan, linked_ch);
			}
		}

		break;
	}
}

static void dma_arc_hs_isr(const struct device *dev)
{
	_Static_assert(sizeof(unsigned int) <= sizeof(uint32_t),
		"unsigned int must be <= uint32_t to safely pass as handle");

	struct arc_dma_data *data = dev->data;
	uint32_t int_status;
	uint32_t bits_to_clear = 0;

	while (true) {
		int_status = z_arc_v2_aux_reg_read(DMA_C_INTSTAT_AUX);

		if (int_status == 0) {
			break;
		}

		/* clear the interrupt */
		z_arc_v2_aux_reg_write(DMA_C_INTSTAT_CLR_AUX, int_status);

		/* Read current done status for group 0 */
		if ((int_status & DMA_C_INTSTAT_DONE) != 0) {
			bits_to_clear = z_arc_v2_aux_reg_read(DMA_S_DONESTATD_AUX(0));
		} else {
			bits_to_clear = 0;
		}

		if (bits_to_clear != 0) {
			/* clear the done status */
			z_arc_v2_aux_reg_write(DMA_S_DONESTATD_CLR_AUX(0), bits_to_clear);
		}

		/* Handle bus error */
		if ((int_status & DMA_C_INTSTAT_BUS_ERR) != 0) {
			LOG_ERR("DMA bus error");
			/* TODO: Implement error handling - abort active channels */
		}

		/* Handle overflow */
		if ((int_status & DMA_C_INTSTAT_OVERFLOW) != 0) {
			LOG_ERR("DMA overflow");
			/* TODO: Implement overflow handling */
		}

		if (((int_status & DMA_C_INTSTAT_DONE) != 0) && (bits_to_clear != 0)) {
			/* Process all set bits */
			while (bits_to_clear != 0) {
				unsigned int bit = find_lsb_set(bits_to_clear) - 1;

				dma_arc_hs_process_handle(dev, data, bit);
				bits_to_clear &= ~(1U << bit);
			}
		}
	}
}

static const struct dma_driver_api dma_arc_hs_api = {
	.config = dma_arc_hs_config,
	.start = dma_arc_hs_start,
	.stop = dma_arc_hs_stop,
	.suspend = NULL,
	.resume = NULL,
	.get_status = dma_arc_hs_get_status,
	.get_attribute = dma_arc_hs_get_attribute,
	.chan_filter = NULL,
	.chan_release = NULL,
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
		data->channels[i].state = ARC_DMA_FREE;
		data->channels[i].callback = NULL;
		data->channels[i].callback_arg = NULL;
		data->channels[i].block_count = 0;
		/* Spinlocks are zero-initialized by default in Zephyr */
	}

	/* CLear all pending DMA done status bits*/
	uint32_t num_groups = DIV_ROUND_UP(config->descriptors, 32);

	for (uint32_t group = 0; group < num_groups; group++) {
		z_arc_v2_aux_reg_write(DMA_S_DONESTATD_CLR_AUX(group), 0xFFFFFFFF);
	}

	/* Disable all channels before reconfiguration*/
	for (i = 0; i < config->channels; i++) {
		z_arc_v2_aux_reg_write(DMA_S_STATC_AUX(i), 0x0);
	}

	dma_arc_hs_config_hw();

	for (i = 0; i < config->channels; i++) {
		dma_arc_hs_init_channel_hw(i, 0, config->descriptors - 1);
	}

	/* Configure and enable interrupt */
	config->irq_config();

	LOG_DBG("ARC DMA initialized successfully");
	return 0;
}

#define CONFIGURE_ARC_DMA_IRQ(idx, inst)                                                           \
	IF_ENABLED(DT_INST_IRQ_HAS_IDX(inst, idx), ( \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(inst, idx, irq), \
			    DT_INST_IRQ_BY_IDX(inst, idx, priority), \
			    dma_arc_hs_isr, \
			    DEVICE_DT_INST_GET(inst), 0); \
		irq_enable(DT_INST_IRQ_BY_IDX(inst, idx, irq)); \
	))

#define ARC_DMA_INIT(inst)                                                                         \
	static void arc_dma_irq_config_##inst(void);                                               \
                                                                                                   \
	static const struct arc_dma_config arc_dma_config_##inst = {                               \
		.base = DMA_AUX_BASE, /*not in addressable memory*/                                \
		.channels = DT_INST_PROP(inst, dma_channels),                                      \
		.descriptors = DT_INST_PROP(inst, dma_descriptors),                                \
		.max_burst_size = DT_INST_PROP(inst, max_burst_size),                              \
		.max_pending_transactions = DT_INST_PROP(inst, max_pending_transactions),          \
		.buffer_size = DT_INST_PROP(inst, buffer_size),                                    \
		.max_block_size = DT_INST_PROP(inst, dma_max_block_size),                          \
		.coherency_support = DT_INST_PROP(inst, coherency_support),                        \
		.buffer_address_alignment = DT_INST_PROP(inst, buffer_address_alignment),          \
		.irq_config = arc_dma_irq_config_##inst,                                           \
	};                                                                                         \
                                                                                                   \
	/* Allocate only the needed number of channels */                                          \
	static struct arc_dma_channel arc_dma_channels_##inst[DT_INST_PROP(inst, dma_channels)];   \
	/* Statically allocate transfer blocks - sized by max descriptors */                       \
	static struct dma_block_config arc_dma_blocks_##inst[DT_INST_PROP(inst, dma_descriptors)]; \
	static struct arc_dma_data arc_dma_data_##inst = {                                         \
		.channels = arc_dma_channels_##inst,                                               \
		.transfer_blocks = arc_dma_blocks_##inst,                                          \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, dma_arc_hs_init, NULL, &arc_dma_data_##inst,                   \
			      &arc_dma_config_##inst, POST_KERNEL, CONFIG_DMA_INIT_PRIORITY,       \
			      &dma_arc_hs_api);                                                    \
                                                                                                   \
	static void arc_dma_irq_config_##inst(void)                                                \
	{                                                                                          \
		LISTIFY(DT_NUM_IRQS(DT_DRV_INST(inst)),                                            \
			CONFIGURE_ARC_DMA_IRQ, (), inst);             \
	}

DT_INST_FOREACH_STATUS_OKAY(ARC_DMA_INIT)
