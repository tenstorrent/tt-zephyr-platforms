/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DMA_ARC_H
#define DMA_ARC_H

#include <stdint.h>
#include <stdbool.h>

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
#define DMA_S_BASEC_AUX(ch) (0xd00 + 0x83 + (ch))
#define DMA_S_LASTC_AUX(ch) (0xd00 + 0x84 + (ch))
#define DMA_S_STATC_AUX(ch) (0xd00 + 0x86 + (ch))
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
static inline void arc_dma_config_hw(void)
{
	uint32_t reg = 0;

	reg = (0xf << 4);                   /* Set LBU read transaction limit to max */
	reg |= (0x4 << 8);                  /* Set max burst length to 16 (max supported) */
	arc_write_aux(DMA_S_CTRL_AUX, reg); /* Apply settings above */
}

static inline void arc_dma_init_channel_hw(uint32_t dma_ch, uint32_t base, uint32_t last)
{
	arc_write_aux(DMA_S_BASEC_AUX(dma_ch), base);
	arc_write_aux(DMA_S_LASTC_AUX(dma_ch), last);
	arc_write_aux(DMA_S_STATC_AUX(dma_ch), 0x1); /* Enable dma_ch */
}

static inline void arc_dma_start_hw(uint32_t dma_ch, const void *p_src, void *p_dst, uint32_t len,
				    uint32_t attr)
{
	arc_write_aux(DMA_C_CHAN_AUX, dma_ch);
	arc_write_aux(DMA_C_SRC_AUX, (uint32_t)p_src);
	arc_write_aux(DMA_C_DST_AUX, (uint32_t)p_dst);
	arc_write_aux(DMA_C_ATTR_AUX, attr);
	arc_write_aux(DMA_C_LEN_AUX, len);
}

static inline uint32_t arc_dma_get_handle_hw(void)
{
	return arc_read_aux(DMA_C_HANDLE_AUX);
}

static inline uint32_t arc_dma_poll_busy_hw(void)
{
	return arc_read_aux(DMA_C_STAT_AUX);
}

static inline void arc_dma_clear_done_hw(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = (1 << (handle & 0x1f));

	arc_write_aux(DMA_S_DONESTATD_CLR_AUX(d), b);
}

static inline uint32_t arc_dma_get_done_hw(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = handle & 0x1f;

	uint32_t volatile state = (arc_read_aux(DMA_S_DONESTATD_AUX(d & 0x7))) >> b;

	return state & 0x1;
}

#endif /* DMA_ARC_H */
