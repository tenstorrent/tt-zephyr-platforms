/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GDDR_H_
#define _GDDR_H_

#include "gddr_telemetry_table.h"

#define MIN_GDDR_SPEED             12000
#define MAX_GDDR_SPEED             20000
#define GDDR_SPEED_TO_MEMCLK_RATIO 16
#define NUM_GDDR 8

/* MRISC FW telemetry base addr */
#define GDDR_TELEMETRY_TABLE_ADDR 0x8000

#define RISC_CTRL_A_SCRATCH_0__REG_ADDR 0xFFB14010
#define RISC_CTRL_A_SCRATCH_1__REG_ADDR 0xFFB14014
#define MRISC_INIT_STATUS               RISC_CTRL_A_SCRATCH_0__REG_ADDR
#define MRISC_POST_CODE                 RISC_CTRL_A_SCRATCH_1__REG_ADDR


#define MRISC_INIT_FINISHED 0xdeadbeef
#define MRISC_INIT_FAILED   0xfa11
#define MRISC_INIT_BEFORE   0x11111111
#define MRISC_INIT_STARTED  0x0
#define MRISC_INIT_TIMEOUT  1000 /* In ms */

void read_gddr_telemetry_table(uint8_t gddr_inst, gddr_telemetry_table_t *gddr_telemetry);
void SetAxiEnable(uint8_t gddr_inst, uint8_t noc2axi_port, bool axi_enable);
int LoadMriscFw(uint8_t gddr_inst, uint8_t *fw_image, uint32_t fw_size);
int LoadMriscFwCfg(uint8_t gddr_inst, uint8_t *fw_cfg_image, uint32_t fw_cfg_size);
void ReleaseMriscReset(uint8_t gddr_inst);
static inline uint32_t GetGddrSpeedFromCfg(uint8_t *fw_cfg_image)
{
	/* GDDR speed is the second DWORD of the MRISC FW Config table */
	uint32_t *fw_cfg_dw = (uint32_t *)fw_cfg_image;
	return fw_cfg_dw[1];
}
uint32_t MriscRegRead32(uint8_t gddr_inst, uint32_t addr);
void MriscRegWrite32(uint8_t gddr_inst, uint32_t addr, uint32_t val);
uint32_t GetDramMask(void);

#endif
