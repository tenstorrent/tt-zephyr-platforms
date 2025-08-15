/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "eth.h"
#include "gddr.h"
#include "harvesting.h"
#include "init.h"
#include "noc.h"
#include "noc2axi.h"
#include "noc_dma.h"

#include <tenstorrent/sys_init_defines.h>

#define ERISC_L1_SIZE  (512 * 1024)
#define MRISC_L1_SIZE  (128 * 1024)
#define TENSIX_L1_SIZE (1536 * 1024)

#define ARC_NOC0_X 8
#define ARC_NOC0_Y 0

/* use any arbitrary non-harvested tensix core as the source for wiping l1s */
static const uint8_t tensix_x = 1;
static const uint8_t tensix_y = 2;

/* this function first clears an arbitrary non-harvested tensix core and do a NOC DMA broadcast to
 * clear all remaining tensix l1s
 */
static void wipe_tensix_l1(void)
{
	uint64_t addr = 0;

	uint8_t sram_buffer[SCRATCHPAD_SIZE] __aligned(4);

	/* wipe SCRATCHPAD_SIZE of the chosen tensix */
	memset(sram_buffer, 0, SCRATCHPAD_SIZE);
	noc_dma_read(tensix_x, tensix_y, addr, ARC_NOC0_X, ARC_NOC0_Y, (uintptr_t)sram_buffer,
		     SCRATCHPAD_SIZE, true);

	/* wipe entire L1 of the chosen tensix */
	uint32_t offset = SCRATCHPAD_SIZE;

	while (offset < TENSIX_L1_SIZE) {
		uint32_t size = MIN(offset, TENSIX_L1_SIZE - offset);

		noc_dma_write(tensix_x, tensix_y, addr, tensix_x, tensix_y, offset, size, true);
		offset += offset;
	}

	/* clear all remaining tensix L1 using the already-cleared L1 as a source */
	noc_dma_broadcast(tensix_x, tensix_y, addr, TENSIX_L1_SIZE);
}

/* This function assumes that tensix L1s have already been cleared */
static void wipe_mrisc_l1(void)
{
	uint8_t noc_id = 0;
	uint64_t addr = 0;
	uint32_t dram_mask = GetDramMask();

	for (uint32_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			for (uint32_t noc2axi_port = 0; noc2axi_port < NUM_MRISC_NOC2AXI_PORT;
			     noc2axi_port++) {
				uint8_t x, y;

				GetGddrNocCoords(gddr_inst, noc2axi_port, noc_id, &x, &y);
				/* AXI enable must not be set, using MRISC address 0 */
				noc_dma_write(tensix_x, tensix_y, addr, x, y, addr, MRISC_L1_SIZE,
					      true);
			}
		}
	}
}

/* This function assumes that tensix L1s have already been cleared */
static void wipe_erisc_l1(void)
{
	uint8_t noc_id = 0;
	uint64_t addr = 0;

	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			uint8_t x, y;

			GetEthNocCoords(eth_inst, noc_id, &x, &y);
			noc_dma_write(tensix_x, tensix_y, addr, x, y, addr, ERISC_L1_SIZE, true);
		}
	}
}

static int wipe_l1(void)
{
	wipe_tensix_l1();
	wipe_mrisc_l1();
	wipe_erisc_l1();

	return 0;
}

SYS_INIT_APP(wipe_l1);
