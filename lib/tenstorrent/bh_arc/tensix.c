/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tensix.h"
#include "noc2axi.h"

#define DBG_INSTRN_BUF_CTRL0 0xFFB120A0
#define DBG_INSTRN_BUF_CTRL1 0xFFB120A4

void tensix_inject_instruction(uint32_t instruction, uint8_t thread, bool broadcast, uint8_t noc_x,
			       uint8_t noc_y)
{
	uint8_t ring = 0;
	uint8_t noc_tlb = 0;
	uint32_t override = 1u << thread;
	uint32_t write = 1u << (thread + 4);

	if (broadcast) {
		NOC2AXITensixBroadcastTlbSetup(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0,
					       kNoc2AxiOrderingStrict);
	} else {
		NOC2AXITlbSetup(ring, noc_tlb, noc_x, noc_y, DBG_INSTRN_BUF_CTRL0);
	}

	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0, 0);
	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL1, instruction);

	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0, override);
	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0, override | write);
	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0, override);

	NOC2AXIWrite32(ring, noc_tlb, DBG_INSTRN_BUF_CTRL0, 0);
}
