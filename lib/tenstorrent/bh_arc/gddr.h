/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _GDDR_H_
#define _GDDR_H_

#define MIN_GDDR_SPEED 12000
#define MAX_GDDR_SPEED 20000
#define GDDR_SPEED_TO_MEMCLK_RATIO 16

void SetAxiEnable(uint8_t gddr_inst, uint8_t noc2axi_port, bool axi_enable);
int LoadMriscFw(uint8_t gddr_inst, uint8_t *fw_image, uint32_t fw_size);
int LoadMriscFwCfg(uint8_t gddr_inst, uint8_t *fw_cfg_image, uint32_t fw_cfg_size);
void ReleaseMriscReset(uint8_t gddr_inst);
static inline uint32_t GetGddrSpeedFromCfg(uint8_t *fw_cfg_image) {
  // GDDR speed is the second DWORD of the MRISC FW Config table
  uint32_t *fw_cfg_dw = (uint32_t *)fw_cfg_image;
  return fw_cfg_dw[1];
}
#endif