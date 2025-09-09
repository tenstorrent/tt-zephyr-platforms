/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_arc_dma

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/dma.h>

#include "arc.h"

LOG_MODULE_REGISTER(tt_bh_arc_dma, CONFIG_TT_BH_ARC_DMA_LOG_LEVEL);

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
#define DMA_S_BASEC_AUX(ch) (0xd00 + 0x83 + (ch))
#define DMA_S_LASTC_AUX(ch) (0xd00 + 0x84 + (ch))
#define DMA_S_STATC_AUX(ch) (0xd00 + 0x86 + (ch))
#define DMA_S_DONESTATD_AUX(d)                                                                     \
	(0xd00 + 0x20 + (d)) /* Descriptor seclection. Each D stores descriptors d*32 +: 32 */
#define DMA_S_DONESTATD_CLR_AUX(d) (0xd00 + 0x40 + (d))

#define ARC_DMA_NP_ATTR       (1 << 3) /* Enable non posted writes */
#define ARC_DMA_SET_DONE_ATTR (1 << 0) /* Set done without triggering interrupt */

#define NUM_CHANNELS CONFIG_DMA_TT_BH_ARC_CHANNELS
#define NUM_DESCRIPTORS 256

/* compile time configurable */
struct tt_bh_arc_dma_config {
	uint32_t irq;
};

/* runtime per-channel data */
struct tt_bh_arc_dma_chan {
	bool busy;
	struct dma_config *cfg;
	uint32_t handle;
};

/* runtime driver data */
struct tt_bh_arc_dma_data {
	struct tt_bh_arc_dma_chan channels[NUM_CHANNELS];
	dma_callback_t dma_cb_table[NUM_DESCRIPTORS];
	struct k_mutex dma_cb_mutex;
};

/* helper functions */

static void arc_dma_config(void)
{
	uint32_t reg = 0;

	reg = (0xf << 4);                 /* Set LBU read transaction limit to max */
	reg |= (0x4 << 8);                /* Set max burst length to 16 (max supported) */
	ArcWriteAux(DMA_S_CTRL_AUX, reg); /* Apply settings above */
}

static void arc_dma_init_ch(uint32_t dma_ch, uint32_t base, uint32_t last)
{
	ArcWriteAux(DMA_S_BASEC_AUX(dma_ch), base);
	ArcWriteAux(DMA_S_LASTC_AUX(dma_ch), last);
	ArcWriteAux(DMA_S_STATC_AUX(dma_ch), 0x1); /* Enable dma_ch */
}

static void arc_dma_start(uint32_t dma_ch, const void *p_src,
void *p_dst, uint32_t len, uint32_t attr)
{
	ArcWriteAux(DMA_C_CHAN_AUX, dma_ch);
	arc_dma_next(p_src, p_dst, len, attr);
}

static void arc_dma_next(const void *p_src, void *p_dst, uint32_t len, uint32_t attr)
{
	ArcWriteAux(DMA_C_SRC_AUX, (uint32_t)p_src);
	ArcWriteAux(DMA_C_DST_AUX, (uint32_t)p_dst);
	ArcWriteAux(DMA_C_ATTR_AUX, attr);
	ArcWriteAux(DMA_C_LEN_AUX, len);
}

static uint32_t arc_dma_get_handle(void)
{
	return ArcReadAux(DMA_C_HANDLE_AUX);
}

static uint32_t arc_dma_poll_busy(void)
{
	return ArcReadAux(DMA_C_STAT_AUX);
}

static void arc_dma_clear_done(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = (1 << (handle & 0x1f));

	ArcWriteAux(DMA_S_DONESTATD_CLR_AUX(d), b);
}

static uint32_t arc_dma_get_done(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = handle & 0x1f;

	uint32_t state = (ArcReadAux(DMA_S_DONESTATD_AUX(d & 0x7))) >> b;

	return state & 0x1;
}

/* DMA API implementations */

static void tt_bh_arc_dma_isr(const struct device *dev)
{
	struct tt_bh_arc_dma_data *data = dev->data;
	uint32_t stat, c;
	int h = 0;  /* Descriptor counter */

	k_mutex_lock(&data->dma_cb_mutex, K_FOREVER);
	for (int i = 0; i < (NUM_DESCRIPTORS / 32); i++) {
		stat = ArcReadAux(DMA_S_DONESTATD_AUX(i));
		if (stat != 0) {
			c = 0;
			for (int j = 0; j < 32; j++, h++, stat >>= 1) {
				if ((stat & 1) && (data->dma_cb_table[h] != NULL)) {
					data->dma_cb_table[h](dev, data->dma_cb_table[h], 0, 0);
					data->dma_cb_table[h] = NULL;
					c |= (1 << j);  /* Collect bits to clear */
				}
			}
			if (c != 0) {
				ArcWriteAux(DMA_S_DONESTATD_CLR_AUX(i), c);
			}
		}
	}
	k_mutex_unlock(&data->dma_cb_mutex);
}

static int tt_bh_arc_dma_config(const struct device *dev, uint32_t channel,
				 struct dma_config *cfg)
{
	struct tt_bh_arc_dma_data *data = dev->data;
	struct tt_bh_arc_dma_chan *chan;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	chan = &data->channels[channel];

	if (chan->busy) {
		return -EBUSY;
	}

	if (cfg->channel_direction != MEMORY_TO_MEMORY) {
		return -ENOTSUP;
	}

	if (cfg->block_count != 1) {
		return -ENOTSUP;
	}

	chan->cfg = cfg;
	chan->busy = false;

	return 0;
}

static int tt_bh_arc_dma_start(const struct device *dev, uint32_t channel)
{
	struct tt_bh_arc_dma_data *data = dev->data;
	struct tt_bh_arc_dma_chan *chan;
	struct dma_block_config *blk;
	uint32_t attr = ARC_DMA_NP_ATTR;
	bool use_interrupt = false;
	uint32_t dma_status;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	chan = &data->channels[channel];

	if (chan->busy || chan->cfg == NULL) {
		return -EBUSY;
	}

	blk = chan->cfg->head_block;
	if (blk == NULL) {
		return -EINVAL;
	}

	chan->busy = true;

	use_interrupt = (chan->cfg->dma_callback != NULL);

	if (use_interrupt) {
		/* Trigger interrupt on done */
		attr = ARC_DMA_NP_ATTR;
	} else {
		/* Set done without interrupt, will poll */
		attr |= ARC_DMA_SET_DONE_ATTR;
	}

	arc_dma_start(channel, (const void *)blk->source_address,
			   (void *)blk->dest_address, blk->block_size, attr);

	chan->handle = arc_dma_get_handle();

	if (!use_interrupt) {
		/* Poll with timeout (100ms) */
		int64_t end_time = k_uptime_get() + 100;

		do {
			dma_status = arc_dma_get_done(chan->handle);
		} while (dma_status == 0 && k_uptime_get() < end_time);

		if (dma_status != 0) {
			arc_dma_clear_done(chan->handle);
			chan->busy = false;
			return 0;
		}

		chan->busy = false;
		return -ETIMEDOUT;
	}

	/* Async: interrupt will handle completion */
	return 0;
}

static int tt_bh_arc_dma_stop(const struct device *dev, uint32_t channel)
{
	struct tt_bh_arc_dma_data *data = dev->data;
	struct tt_bh_arc_dma_chan *chan;

	if (channel >= NUM_CHANNELS) {
		return -EINVAL;
	}

	chan = &data->channels[channel];

	if (!chan->busy) {
		return 0;
	}

	ArcWriteAux(DMA_C_CHAN_AUX, channel);
	if (arc_dma_poll_busy() != 0) {
		LOG_ERR("Cannot stop busy channel %u", channel);
		return -EBUSY;
	}

	arc_dma_clear_done(chan->handle);
	chan->busy = false;

	return 0;
}

static int tt_bh_arc_dma_get_status(const struct device *dev, uint32_t channel,
					 struct dma_status *status)
{
	struct tt_bh_arc_dma_data *data = dev->data;
	struct tt_bh_arc_dma_chan *chan;

	if (channel >= NUM_CHANNELS || status == NULL) {
		return -EINVAL;
	}

	chan = &data->channels[channel];

	status->busy = chan->busy;
	status->dir = MEMORY_TO_MEMORY;
	status->pending_length = 0; /* No partial transfer support */

	return 0;
}

static const struct dma_driver_api tt_bh_arc_dma_api = {
	.config = tt_bh_arc_dma_config,
	.start = tt_bh_arc_dma_start,
	.stop = tt_bh_arc_dma_stop,
	.get_status = tt_bh_arc_dma_get_status,
};

static int tt_bh_arc_dma_init(const struct device *dev)
{
	const struct tt_bh_arc_dma_config *config = dev->config;
	struct tt_bh_arc_dma_data *data = dev->data;

	if (!IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	k_mutex_init(&data->dma_cb_mutex);

	arc_dma_config();

	for (uint32_t ch = 0; ch < NUM_CHANNELS; ch++) {
		arc_dma_init_ch(ch, ch, ch); /* One descriptor per channel */
	}

	/* Set up interrupt if configured */
	if (config->irq != 0) { /* Assuming 0 means no IRQ */
		IRQ_CONNECT(config->irq, 0, tt_bh_arc_dma_isr, dev, 0);
		irq_enable(config->irq);
	}

	return 0;
}

#define TT_BH_ARC_DMA_INIT(inst) \
	static const struct tt_bh_arc_dma_config tt_bh_arc_dma_config_##inst = { \
		.irq = DT_INST_IRQN(inst), \
	}; \
	\
	static struct tt_bh_arc_dma_data tt_bh_arc_dma_data_##inst; \
	\
	DEVICE_DT_INST_DEFINE(inst, tt_bh_arc_dma_init, NULL, \
				   &tt_bh_arc_dma_data_##inst, &tt_bh_arc_dma_config_##inst, \
				   POST_KERNEL, CONFIG_DMA_INIT_PRIORITY, &tt_bh_arc_dma_api);

DT_INST_FOREACH_STATUS_OKAY(TT_BH_ARC_DMA_INIT)
