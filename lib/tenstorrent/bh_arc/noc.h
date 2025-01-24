/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NOC_H_INLCUDED
#define NOC_H_INCLUDED

#include <stdint.h>

#define NUM_NOCS   2
#define NOC_X_SIZE 17
#define NOC_Y_SIZE 12

#define NIU_CFG_0_AXI_SLAVE_ENABLE 15

#define NOC0_X_TO_NOC1(x) (NOC_X_SIZE - (x) - 1)
#define NOC0_Y_TO_NOC1(y) (NOC_Y_SIZE - (y) - 1)

uint64_t NiuRegsBase(uint8_t px, uint8_t py, uint8_t noc_id);
uint64_t OverlayRegsBase(uint8_t px, uint8_t py); /* Returns 0 if node doesn't support overlay. */

uint8_t PhysXToNoc(uint8_t px, uint8_t noc_id);
uint8_t PhysYToNoc(uint8_t py, uint8_t noc_id);
uint8_t NocToPhysX(uint8_t nx, uint8_t noc_id);
uint8_t NocToPhysY(uint8_t ny, uint8_t noc_id);

uint8_t TensixPhysXToNoc(uint8_t px, uint8_t noc_id);
uint8_t TensixPhysYToNoc(uint8_t py, uint8_t noc_id);
uint8_t NocToTensixPhysX(uint8_t x, uint8_t noc_id);
void GetGddrNocCoords(uint8_t gddr_inst, uint8_t noc2axi_port, uint8_t noc_id, uint8_t *x,
		      uint8_t *y);
void GetEthNocCoords(uint8_t eth_inst, uint8_t noc_id, uint8_t *x, uint8_t *y);
void GetSerdesNocCoords(uint8_t serdes_inst, uint8_t noc_id, uint8_t *x, uint8_t *y);

#endif
