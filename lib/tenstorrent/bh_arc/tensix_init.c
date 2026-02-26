/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "bh_reset.h"
#include "noc2axi.h"
#include "noc_init.h"

#include <stdint.h>

#include <tenstorrent/post_code.h>
#include <tenstorrent/sys_init_defines.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_tt_bh_noc.h>
#include <zephyr/init.h>

#define ARC_NOC0_X 8
#define ARC_NOC0_Y 0

#define TENSIX_L1_SIZE (1536 * 1024)

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));
static const struct device *const dma_noc = DEVICE_DT_GET(DT_NODELABEL(dma1));

/* Enable CG_CTRL_EN in each non-harvested Tensix node and set CG hystersis to 2. */
/* This requires NOC init so that broadcast is set up properly. */

/* We enable CG for all blocks, but for reference the bit assignments are */
/* 0 - Register Blocks */
/* 1 - FPU */
/* 2 - FPU M Tile */
/* 3 - FPU SFPU */
/* 4 - Mover */
/* 5 - Packers */
/* 6 - Unpacker 0 */
/* 7 - Unpacker 1 */
/* 8 - X Search */
/* 9 - Thread Controller */
/* 10 - TRISC 0 */
/* 11 - TRISC 1 */
/* 12 - TRISC 2 */
/* 13 - L1 Return Muxes */
/* 14 - Instruction Thread */
/* 15 - L1 Banks */
/* 16 - Src B */

static void EnableTensixCG(void)
{
	uint8_t ring = 0;
	uint8_t noc_tlb = 0;

	/* CG hysteresis for the blocks. (Some share a field.) */
	/* Set them all to 2. */
	uint32_t cg_ctrl_hyst0 = 0xFFB12070;
	uint32_t cg_ctrl_hyst1 = 0xFFB12074;
	uint32_t cg_ctrl_hyst2 = 0xFFB1207C;

	uint32_t all_blocks_hyst_2 = 0x02020202;

	/* Enable CG for all blocks. */
	uint32_t cg_ctrl_en = 0xFFB12244;
	uint32_t enable_all_tensix_cg = 0xFFFFFFFF; /* Only bits 0-16 are used. */

	NOC2AXITensixBroadcastTlbSetup(ring, noc_tlb, cg_ctrl_en, kNoc2AxiOrderingStrict);

	NOC2AXIWrite32(ring, noc_tlb, cg_ctrl_hyst0, all_blocks_hyst_2);
	NOC2AXIWrite32(ring, noc_tlb, cg_ctrl_hyst1, all_blocks_hyst_2);
	NOC2AXIWrite32(ring, noc_tlb, cg_ctrl_hyst2, all_blocks_hyst_2);

	NOC2AXIWrite32(ring, noc_tlb, cg_ctrl_en, enable_all_tensix_cg);
}

/**
 * @brief Zeros the l1 of every non-harvested tensix core
 *
 * First zero the l1 of an arbitrary non-harvested tensix core, then broadcasts the zero'd l1 to
 * all other non-harvested tensix cores. This approach is faster than iterating over all tensix
 * cores sequentially to clear each l1.
 */
static void wipe_l1(void)
{
	uint64_t addr = 0;
	uint8_t tensix_x, tensix_y;
	/* NOC2AXI to Tensix L1 transactions must be aligned to 64 bytes */
	uint8_t sram_buffer[CONFIG_TT_BH_ARC_SCRATCHPAD_SIZE] __aligned(64);

	GetEnabledTensix(&tensix_x, &tensix_y);

	/* wipe SCRATCHPAD_SIZE of the chosen tensix */
	memset(sram_buffer, 0, sizeof(sram_buffer));

	struct tt_bh_dma_noc_coords coords =
		tt_bh_dma_noc_coords_init(tensix_x, tensix_y, ARC_NOC0_X, ARC_NOC0_Y);

	struct dma_block_config block = {
		.source_address = addr,
		.dest_address = (uintptr_t)sram_buffer,
		.block_size = sizeof(sram_buffer),
	};

	struct dma_config config = {
		.channel_direction = MEMORY_TO_PERIPHERAL,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
		.head_block = &block,
		.user_data = &coords,
	};

	dma_config(dma_noc, 1, &config);
	dma_start(dma_noc, 1);

	/* wipe entire L1 of the chosen tensix */
	uint32_t offset = sizeof(sram_buffer);

	while (offset < TENSIX_L1_SIZE) {
		uint32_t size = MIN(offset, TENSIX_L1_SIZE - offset);

		config.channel_direction = PERIPHERAL_TO_MEMORY;
		coords.dest_x = tensix_x;
		coords.dest_y = tensix_y;
		block.dest_address = offset;
		block.block_size = size;

		dma_config(dma_noc, 1, &config);
		dma_start(dma_noc, 1);

		offset += offset;
	}

	/* clear all remaining tensix L1 using the already-cleared L1 as a source */
	config.channel_direction = TT_BH_DMA_NOC_CHANNEL_DIRECTION_BROADCAST;
	block.source_address = addr;
	block.dest_address = addr;
	block.block_size = TENSIX_L1_SIZE;

	dma_config(dma_noc, 1, &config);
	dma_start(dma_noc, 1);
}

void TensixInit(void)
{
	if (!tt_bh_fwtable_get_fw_table(fwtable_dev)->feature_enable.cg_en) {
		EnableTensixCG();
	}

	/* wipe_l1() isn't here because it's only needed on boot & board reset. */
}

static int tensix_init(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPD);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* In cable fault mode, tensix tiles are clock-gated - skip init
	 * since tiles are not active.
	 */
	if (is_cable_fault_mode()) {
		return 0;
	}

	TensixInit();

	wipe_l1();

	return 0;
}
SYS_INIT_APP(tensix_init);
