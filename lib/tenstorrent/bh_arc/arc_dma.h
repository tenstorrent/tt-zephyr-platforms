/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ARC_DMA_H
#define ARC_DMA_H

#include <stdint.h>
#include <stdbool.h>

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

#define ARC_DMA_NP_ATTR       (1 << 3) /*Enable non posted writes */
#define ARC_DMA_SET_DONE_ATTR (1 << 0) /* Set done without triggering interrupt */

void ArcDmaConfig(void);
void ArcDmaInitCh(uint32_t dma_ch, uint32_t base, uint32_t last);
void ArcDmaStart(uint32_t dma_ch, const void *p_src, void *p_dest, uint32_t len, uint32_t attr);
void ArcDmaNext(const void *p_src, void *p_dest, uint32_t len, uint32_t attr);
uint32_t ArcDmaGetHandle(void);
uint32_t ArcDmaPollBusy(void);
void ArcDmaClearDone(uint32_t handle);
uint32_t ArcDmaGetDone(uint32_t handle);
bool ArcDmaTransfer(const void *src, void *dst, uint32_t size);
#endif
