/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/util.h>

#include "noc.h"
#include "noc2axi.h"
#include "fw_table.h"

#define NIU_CFG_0 0x0

#define ROUTER_CFG(n) ((n) + 1)

#define CLOCK_GATING_EN                 0
#define NIU_CFG_0_TILE_HEADER_STORE_OFF 13 // NOC2AXI only
#define NIU_CFG_0_AXI_SLAVE_ENABLE      15
#define STREAM_PERF_CONFIG_REG_INDEX    35

static const uint8_t kTlbIndex = 0;

static const uint32_t kFirstCfgRegIndex = 0x100 / sizeof(uint32_t);

static volatile void *SetupNiuTlbPhys(uint8_t tlb_index, uint8_t px, uint8_t py, uint8_t noc_id)
{
  uint64_t regs = NiuRegsBase(px, py, noc_id);

  NOC2AXITlbSetup(noc_id, tlb_index, PhysXToNoc(px, noc_id), PhysYToNoc(py, noc_id), regs);

  return GetTlbWindowAddr(noc_id, tlb_index, regs);
}

static uint32_t ReadNocCfgReg(volatile void* regs, uint32_t cfg_reg_index) {
  return ((volatile uint32_t*)regs)[kFirstCfgRegIndex+cfg_reg_index];
}

static void WriteNocCfgReg(volatile void *regs, uint32_t cfg_reg_index, uint32_t value) {
  ((volatile uint32_t*)regs)[kFirstCfgRegIndex+cfg_reg_index] = value;
}

static void EnableOverlayCg(uint8_t tlb_index, uint8_t px, uint8_t py) {
  uint8_t ring = 0; // Either NOC ring works, there's only one overlay.

  uint64_t overlay_regs_base = OverlayRegsBase(px, py);
  if (overlay_regs_base != 0) {
    NOC2AXITlbSetup(ring, tlb_index, PhysXToNoc(px, ring), PhysYToNoc(py, ring), overlay_regs_base);

    volatile uint32_t *regs = GetTlbWindowAddr(ring, tlb_index, overlay_regs_base);

    // Set stream[0].STREAM_PERF_CONFIG.CLOCK_GATING_EN = 1, leave other fields at defaults.
    regs[STREAM_PERF_CONFIG_REG_INDEX] |= 1u << CLOCK_GATING_EN;
  }
}

void NocInit() {
  // ROUTER_CFG_1,2 are a 64-bit mask for column broadcast disable
  // ROUTER_CFG_3,4 are a 64-bit mask for row broadcast disable
  // A node will not receive broadcasts if it is in a disabled row or column.

  // Disable broadcast to west GDDR, L2CPU/security/ARC, east GDDR columns.
  static const uint32_t router_cfg_1[NUM_NOCS] = {
    BIT(0) | BIT(8) | BIT(9),
    BIT(NOC0_X_TO_NOC1(0)) | BIT(NOC0_X_TO_NOC1(8)) | BIT(NOC0_X_TO_NOC1(9)),
  };

  // Disable broadcast to ethernet row, PCIE/SERDES row.
  static const uint32_t router_cfg_3[NUM_NOCS] = {
    BIT(0) | BIT(1),
    BIT(NOC0_Y_TO_NOC1(0)) | BIT(NOC0_Y_TO_NOC1(1)),
  };

  uint32_t niu_cfg_0_updates = BIT(NIU_CFG_0_TILE_HEADER_STORE_OFF); // noc2axi tile header double-write feature disable, ignored on all other nodes

  uint32_t router_cfg_0_updates = 0xF << 8;                          // max backoff exp

  bool cg_en = get_fw_table()->feature_enable.cg_en;

  if (cg_en) {
    niu_cfg_0_updates |= BIT(0); // NIU clock gating enable
    router_cfg_0_updates |= BIT(0); // router clock gating enable
  }

  for (uint32_t py = 0; py < NOC_Y_SIZE; py++) {
    for (uint32_t px = 0; px < NOC_X_SIZE; px++) {
      for (uint32_t noc_id = 0; noc_id < NUM_NOCS; noc_id++) {
        volatile uint32_t *noc_regs = SetupNiuTlbPhys(kTlbIndex, px, py, noc_id);

        uint32_t niu_cfg_0 = ReadNocCfgReg(noc_regs, NIU_CFG_0);
        niu_cfg_0 |= niu_cfg_0_updates;
        WriteNocCfgReg(noc_regs, NIU_CFG_0, niu_cfg_0);

        uint32_t router_cfg_0 = ReadNocCfgReg(noc_regs, ROUTER_CFG(0));
        router_cfg_0 |= router_cfg_0_updates;
        WriteNocCfgReg(noc_regs, ROUTER_CFG(0), router_cfg_0);

        WriteNocCfgReg(noc_regs, ROUTER_CFG(1), router_cfg_1[noc_id]);
        WriteNocCfgReg(noc_regs, ROUTER_CFG(2), 0);
        WriteNocCfgReg(noc_regs, ROUTER_CFG(3), router_cfg_3[noc_id]);
        WriteNocCfgReg(noc_regs, ROUTER_CFG(4), 0);
      }

      EnableOverlayCg(kTlbIndex, px, py);
    }
  }
}
