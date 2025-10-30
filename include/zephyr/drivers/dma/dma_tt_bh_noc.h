/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dma.h>

enum tt_bh_dma_noc_channel_direction {
	TT_BH_DMA_NOC_CHANNEL_DIRECTION_BROADCAST = DMA_CHANNEL_DIRECTION_PRIV_START
};

struct tt_bh_dma_noc_coords {
	uint8_t source_x, source_y;
	uint8_t dest_x, dest_y;
};

static inline struct tt_bh_dma_noc_coords
tt_bh_dma_noc_coords_init(uint8_t source_x, uint8_t source_y, uint8_t dest_x, uint8_t dest_y)
{
	return (struct tt_bh_dma_noc_coords){
		.source_x = source_x,
		.source_y = source_y,
		.dest_x = dest_x,
		.dest_y = dest_y,
	};
}
