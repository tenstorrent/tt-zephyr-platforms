/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arc_dma.h"
#include "arc.h"
#include "timer.h"

#include <zephyr/init.h>

void ArcDmaConfig(void)
{
	uint32_t reg = 0;

	reg = (0xf << 4);                 /* Set LBU read transaction limit to max */
	reg = (0x4 << 8);                 /* Set max burst length to 16 (max supported) */
	ArcWriteAux(DMA_S_CTRL_AUX, reg); /* Apply settings above */
}

void ArcDmaInitCh(uint32_t dma_ch, uint32_t base, uint32_t last)
{
	ArcWriteAux(DMA_S_BASEC_AUX(dma_ch), base);
	ArcWriteAux(DMA_S_LASTC_AUX(dma_ch), last);
	ArcWriteAux(DMA_S_STATC_AUX(dma_ch), 0x1); /* Enable dma_ch */
}

void ArcDmaStart(uint32_t dma_ch, const void *p_src, void *p_dst, uint32_t len, uint32_t attr)
{
	ArcWriteAux(DMA_C_CHAN_AUX, dma_ch);
	ArcDmaNext(p_src, p_dst, len, attr);
}

void ArcDmaNext(const void *p_src, void *p_dst, uint32_t len, uint32_t attr)
{
	ArcWriteAux(DMA_C_SRC_AUX, (uint32_t)p_src);
	ArcWriteAux(DMA_C_DST_AUX, (uint32_t)p_dst);
	ArcWriteAux(DMA_C_ATTR_AUX, attr);
	ArcWriteAux(DMA_C_LEN_AUX, len);
}

uint32_t ArcDmaGetHandle(void)
{
	return ArcReadAux(DMA_C_HANDLE_AUX);
}

uint32_t ArcDmaPollBusy(void)
{
	return ArcReadAux(DMA_C_STAT_AUX);
}

void ArcDmaClearDone(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = (1 << (handle & 0x1f));

	ArcWriteAux(DMA_S_DONESTATD_CLR_AUX(d), b);
}

uint32_t ArcDmaGetDone(uint32_t handle)
{
	uint32_t d = handle >> 5;
	uint32_t b = handle & 0x1f;

	uint32_t volatile state = (ArcReadAux(DMA_S_DONESTATD_AUX(d & 0x7))) >> b;
	return state;
}

bool ArcDmaTransfer(const void *src, void *dst, uint32_t size)
{
	const int32_t attr =
		ARC_DMA_SET_DONE_ATTR | ARC_DMA_NP_ATTR; /* Set done with rising interrupt */
	ArcDmaStart(0, src, dst, size, attr);
	uint32_t dma_handle = ArcDmaGetHandle();
	uint32_t dma_status;
	uint64_t end_time = TimerTimestamp() + 100 * WAIT_1MS;

	do {
		dma_status = ArcDmaGetDone(dma_handle);
	} while (dma_status == 0 && TimerTimestamp() < end_time);

	if (dma_status != 0) {
		ArcDmaClearDone(dma_handle);
		return true;
	}

	return false;
}

static int arc_dma_init(void)
{
	if (!IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Initialize ARC DMA */
	ArcDmaConfig();
	ArcDmaInitCh(0, 0, 15);
	return 0;
}
SYS_INIT(arc_dma_init, APPLICATION, 1);
