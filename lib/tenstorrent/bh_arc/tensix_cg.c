/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "noc2axi.h"

#include <stdint.h>

#include <tenstorrent/post_code.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/init.h>

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

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

void EnableTensixCG(void)
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

static int tensix_cg_init(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPD);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	if (!tt_bh_fwtable_get_fw_table(fwtable_dev)->feature_enable.cg_en) {
		return 0;
	}

	EnableTensixCG();

	return 0;
}
SYS_INIT(tensix_cg_init, APPLICATION, 18);
