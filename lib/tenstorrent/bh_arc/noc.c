/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include "noc.h"
#include "compiler.h"

#define NOC_REGS_START_ADDR     0xFFB20000
#define NOC_INSTANCE_OFFSET_BIT 16
#define NOC_OVERLAY_START_ADDR  0xFFB40000

enum NocNodeType {
	kTensixNode, /* or ethernet */
	kNoc2AxiNode,
	kGddrNode,
	kExtraNode,
};

/* This is in the same order as the BH NOC coords spreadsheet: */
/* physical layout with row 11 first. */
static const uint8_t kNodeTypes[NOC_Y_SIZE][NOC_X_SIZE] = {
#define T          kTensixNode
#define TENSIX_ROW T, T, T, T, T, T, T, T, T, T, T, T, T, T /* 14 tensixes in a row */
#define A          kNoc2AxiNode
#define G          kGddrNode
#define X          kExtraNode

	/* GDDR, 14 Tensix, L2CPU core or uncore, GDDR */
	{G, TENSIX_ROW, X, G}, /* 11 */
	{G, TENSIX_ROW, A, G}, /* 10 */
	{G, TENSIX_ROW, A, G}, /*  9 */
	{G, TENSIX_ROW, X, G}, /*  8 */
	{G, TENSIX_ROW, X, G}, /*  7 */
	{G, TENSIX_ROW, A, G}, /*  6 */
	{G, TENSIX_ROW, A, G}, /*  5 */
	{G, TENSIX_ROW, X, G}, /*  4 */

	{G, TENSIX_ROW, A, G}, /*  3 - Security */

	{G, TENSIX_ROW, X, G}, /*  2 */
	{G, TENSIX_ROW, X, G}, /*  1 */

	/* GDDR | PCIE     | SERDES  | SERDES | PCIE      | ARC | GDDR */
	{G, X, X, A, X, X, X, X, X, X, X, X, A, X, X, A, G}, /*  0 */

#undef T
#undef TENSIX_ROW
#undef A
#undef G
#undef X
};

static const uint8_t kPhysXToNoc0[NOC_X_SIZE] = {0, 1,  16, 2,  15, 3,  14, 4, 13,
						 5, 12, 6,  11, 7,  10, 8,  9};
static const uint8_t kPhysYToNoc0[NOC_Y_SIZE] = {0, 1, 11, 2, 10, 3, 9, 4, 8, 5, 7, 6};

static const uint8_t kNoc0XToPhys[NOC_X_SIZE] = {0,  1,  3,  5,  7, 9, 11, 13, 15,
						 16, 14, 12, 10, 8, 6, 4,  2};
static const uint8_t kNoc0YToPhys[NOC_Y_SIZE] = {0, 1, 3, 5, 7, 9, 11, 10, 8, 6, 4, 2};

static enum NocNodeType GetNodeType(uint8_t px, uint8_t py)
{
	uint8_t flipped_py = NOC_Y_SIZE - py - 1;
	return kNodeTypes[flipped_py][px];
}

uint64_t NiuRegsBase(uint8_t px, uint8_t py, uint8_t noc_id)
{
	switch (GetNodeType(px, py)) {
	case kTensixNode:
	case kGddrNode:
		return NOC_REGS_START_ADDR + (noc_id << NOC_INSTANCE_OFFSET_BIT);

	case kNoc2AxiNode:
		return 0xFFFFFFFFFF000000ull;

	case kExtraNode:
		return 0xFF000000ull;

	default:
		unreachable();
	}
}

uint64_t OverlayRegsBase(uint8_t px, uint8_t py)
{
	switch (GetNodeType(px, py)) {
	case kTensixNode:
	case kGddrNode:
		return NOC_OVERLAY_START_ADDR;

	default:
		return 0;
	}
}

uint8_t PhysXToNoc(uint8_t px, uint8_t noc_id)
{
	uint8_t noc0_x = kPhysXToNoc0[px];

	return (noc_id == 0) ? noc0_x : NOC0_X_TO_NOC1(noc0_x);
}

uint8_t PhysYToNoc(uint8_t py, uint8_t noc_id)
{
	uint8_t noc0_y = kPhysYToNoc0[py];

	return (noc_id == 0) ? noc0_y : NOC0_Y_TO_NOC1(noc0_y);
}

uint8_t TensixPhysXToNoc(uint8_t px, uint8_t noc_id)
{
	return PhysXToNoc(px + 1, noc_id);
}

uint8_t TensixPhysYToNoc(uint8_t py, uint8_t noc_id)
{
	return PhysYToNoc(py + 2, noc_id);
}

uint8_t NocToTensixPhysX(uint8_t x, uint8_t noc_id)
{
	for (uint8_t i = 0; i < 14; i++) {
		if (TensixPhysXToNoc(i, noc_id) == x) {
			return i;
		}
	}
	/* Invalid */
	return 0xFF;
}

uint8_t NocToPhysX(uint8_t nx, uint8_t noc_id)
{
	uint8_t noc0_x = (noc_id == 0) ? nx : NOC0_X_TO_NOC1(nx);
	return kNoc0XToPhys[noc0_x];
}

uint8_t NocToPhysY(uint8_t ny, uint8_t noc_id)
{
	uint8_t noc0_y = (noc_id == 0) ? ny : NOC0_Y_TO_NOC1(ny);
	return kNoc0YToPhys[noc0_y];
}

/*
 * Physical Layout of GDDR NOC Nodes:
 * - gIpJ = gddr inst I, noc2axi port J
 * - O*14 = 14 other (non-GDDR) NOC nodes
 * - Bottom left is physical (0,0)
 *
 * g3p2 O*14 g7p0
 * g3p1 O*14 g7p1
 * g3p0 O*14 g7p2
 * g2p2 O*14 g6p0
 * g2p1 O*14 g6p1
 * g2p0 O*14 g6p2
 * g1p2 O*14 g5p0
 * g1p1 O*14 g5p1
 * g1p0 O*14 g5p2
 * g0p2 O*14 g4p0
 * g0p1 O*14 g4p1
 * g0p0 O*14 g4p2
 */
void GetGddrNocCoords(uint8_t gddr_inst, uint8_t noc2axi_port, uint8_t noc_id, uint8_t *x,
		      uint8_t *y)
{
	bool right_gddr_column = gddr_inst / 4; /* false = left gddr col, true = right gddr col */
	uint8_t phys_x = right_gddr_column ? 16 : 0;
	uint8_t phys_y = (gddr_inst % 4) * 3;
	/* Left column numbers noc2axi_port from bottom to top, right column from top to bottom */
	phys_y += right_gddr_column ? 2 - noc2axi_port : noc2axi_port;
	*x = PhysXToNoc(phys_x, noc_id);
	*y = PhysYToNoc(phys_y, noc_id);
}

void GetEthNocCoords(uint8_t eth_inst, uint8_t noc_id, uint8_t *x, uint8_t *y)
{
	*x = PhysXToNoc(eth_inst + 1, noc_id);
	*y = PhysYToNoc(1, noc_id);
}

void GetSerdesNocCoords(uint8_t serdes_inst, uint8_t noc_id, uint8_t *x, uint8_t *y)
{
	/* There are only 2 serdes access points, 1 for serdes 0-2 and 1 for serdes 3-5 */
	uint8_t phys_x = (serdes_inst < 3) ? 3 : 12;
	uint8_t phys_y = 0;

	*x = PhysXToNoc(phys_x, noc_id);
	*y = PhysYToNoc(phys_y, noc_id);
}
