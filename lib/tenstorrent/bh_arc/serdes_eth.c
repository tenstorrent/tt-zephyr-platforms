/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "serdes_eth.h"
#include "noc2axi.h"
#include "noc.h"

static inline void SetupSerdesTlb(uint32_t serdes_inst, uint32_t ring, uint64_t addr)
{
	/* Logical X,Y coordinates */
	uint8_t x, y;

	GetSerdesNocCoords(serdes_inst, ring, &x, &y);

	NOC2AXITlbSetup(ring, SERDES_ETH_SETUP_TLB, x, y, addr);
}

void LoadSerdesEthRegs(uint32_t serdes_inst, uint32_t ring, const SerdesRegData *reg_table,
		       uint32_t reg_count)
{
	SetupSerdesTlb(serdes_inst, 0, SERDES_INST_BASE_ADDR(serdes_inst) + CMN_OFFSET);

	for (uint32_t i = 0; i < reg_count; i++) {
		NOC2AXIWrite32(ring, SERDES_ETH_SETUP_TLB, reg_table[i].addr, reg_table[i].data);
	}
}
