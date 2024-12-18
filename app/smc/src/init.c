/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "init.h"

#include <stdint.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <app_version.h>
#include <tenstorrent/post_code.h>

#include "avs.h"
#include "reg.h"
#include "regulator.h"
#include "status_reg.h"
#include "pll.h"
#include "pvt.h"
#include "pcie.h"
#include "noc.h"
#include "noc2axi.h"
#include "noc_init.h"
#include "arc_dma.h"
#include "tt_boot_fs.h"
#include "spi_controller.h"
#include "spi_eeprom.h"
#include "gddr.h"
#include "smbus_target.h"
#include "cm2bm_msg.h"
#include "irqnum.h"
#include "serdes_eth.h"
#include "eth.h"
#include "gddr.h"
#include "tensix_cg.h"
#include "fw_table.h"
#include "read_only_table.h"
#include "flash_info_table.h"

#define RESET_UNIT_GLOBAL_RESET_REG_ADDR 0x80030000
#define RESET_UNIT_ETH_RESET_REG_ADDR    0x80030008
#define RESET_UNIT_DDR_RESET_REG_ADDR    0x80030010
#define RESET_UNIT_L2CPU_RESET_REG_ADDR  0x80030014

#define RESET_UNIT_TENSIX_RESET_0_REG_ADDR 0x80030020
#define RESET_UNIT_TENSIX_RESET_1_REG_ADDR 0x80030024
#define RESET_UNIT_TENSIX_RESET_2_REG_ADDR 0x80030028
#define RESET_UNIT_TENSIX_RESET_3_REG_ADDR 0x8003002C
#define RESET_UNIT_TENSIX_RESET_4_REG_ADDR 0x80030030
#define RESET_UNIT_TENSIX_RESET_5_REG_ADDR 0x80030034
#define RESET_UNIT_TENSIX_RESET_6_REG_ADDR 0x80030038
#define RESET_UNIT_TENSIX_RESET_7_REG_ADDR 0x8003003C

#define RESET_UNIT_TENSIX_RISC_RESET_0_REG_ADDR 0x80030040

typedef struct {
  uint32_t system_reset_n: 1;
  uint32_t noc_reset_n: 1;
  uint32_t rsvd_0: 5;
  uint32_t refclk_cnt_en: 1;
  uint32_t pcie_reset_n: 2;
  uint32_t rsvd_1: 3;
  uint32_t ptp_reset_n_refclk: 1;
} RESET_UNIT_GLOBAL_RESET_reg_t;

typedef union {
  uint32_t val;
  RESET_UNIT_GLOBAL_RESET_reg_t f;
} RESET_UNIT_GLOBAL_RESET_reg_u;

#define RESET_UNIT_GLOBAL_RESET_REG_DEFAULT (0x00000080)

typedef struct {
  uint32_t eth_reset_n: 14;
  uint32_t rsvd_0: 2;
  uint32_t eth_risc_reset_n: 14;
} RESET_UNIT_ETH_RESET_reg_t;

typedef union {
  uint32_t val;
  RESET_UNIT_ETH_RESET_reg_t f;
} RESET_UNIT_ETH_RESET_reg_u;

#define RESET_UNIT_ETH_RESET_REG_DEFAULT (0x00000000)

typedef struct {
  uint32_t tensix_reset_n: 32;
} RESET_UNIT_TENSIX_RESET_reg_t;

typedef union {
  uint32_t val;
  RESET_UNIT_TENSIX_RESET_reg_t f;
} RESET_UNIT_TENSIX_RESET_reg_u;

#define RESET_UNIT_TENSIX_RESET_REG_DEFAULT (0x00000000)

typedef struct {
  uint32_t ddr_reset_n: 8;
  uint32_t ddr_risc_reset_n: 24;
} RESET_UNIT_DDR_RESET_reg_t;

typedef union {
  uint32_t val;
  RESET_UNIT_DDR_RESET_reg_t f;
} RESET_UNIT_DDR_RESET_reg_u;

#define RESET_UNIT_DDR_RESET_REG_DEFAULT (0x00000000)

typedef struct {
  uint32_t l2cpu_reset_n: 4;
  uint32_t l2cpu_risc_reset_n: 4;
} RESET_UNIT_L2CPU_RESET_reg_t;

typedef union {
  uint32_t val;
  RESET_UNIT_L2CPU_RESET_reg_t f;
} RESET_UNIT_L2CPU_RESET_reg_u;

#define RESET_UNIT_L2CPU_RESET_REG_DEFAULT (0x00000000)

#define SCRATCHPAD_SIZE 0x10000
static uint8_t large_sram_buffer[SCRATCHPAD_SIZE] __attribute__((aligned(4)));

typedef enum {
  kHwInitNotStarted = 0,
  kHwInitStarted = 1,
  kHwInitDone = 2,
  kHwInitError = 3,
} HWInitStatus;

static int SpiReadWrap(uint32_t addr, uint32_t size, uint8_t *dst) {
  SpiBlockRead(addr, size, dst);
  return TT_BOOT_FS_OK;
}

static void InitSpiFS() {
  // Toggle SPI reset to clear state left by bootcode
  SpiControllerReset();

  EepromSetup();
  tt_boot_fs_mount(&boot_fs_data, SpiReadWrap, NULL, NULL);
  SpiBufferSetup();
}

static void DeassertTileResets() {
  RESET_UNIT_GLOBAL_RESET_reg_u global_reset = {.val = RESET_UNIT_GLOBAL_RESET_REG_DEFAULT};
  global_reset.f.noc_reset_n = 1;
  global_reset.f.system_reset_n = 1;
  global_reset.f.pcie_reset_n = 3;
  global_reset.f.ptp_reset_n_refclk = 1;
  WriteReg(RESET_UNIT_GLOBAL_RESET_REG_ADDR, global_reset.val);

  RESET_UNIT_ETH_RESET_reg_u eth_reset = {.val = RESET_UNIT_ETH_RESET_REG_DEFAULT};
  eth_reset.f.eth_reset_n = 0x3fff;
  WriteReg(RESET_UNIT_ETH_RESET_REG_ADDR, eth_reset.val);

  RESET_UNIT_TENSIX_RESET_reg_u tensix_reset = {.val = RESET_UNIT_TENSIX_RESET_REG_DEFAULT};
  tensix_reset.f.tensix_reset_n = 0xffffffff;
  // There are 8 instances of these tensix reset registers
  for (uint32_t i = 0; i < 8; i++) {
    WriteReg(RESET_UNIT_TENSIX_RESET_0_REG_ADDR + i * 4, tensix_reset.val);
  }

  RESET_UNIT_DDR_RESET_reg_u ddr_reset = {.val = RESET_UNIT_DDR_RESET_REG_DEFAULT};
  ddr_reset.f.ddr_reset_n = 0xff;
  WriteReg(RESET_UNIT_DDR_RESET_REG_ADDR, ddr_reset.val);

  RESET_UNIT_L2CPU_RESET_reg_u l2cpu_reset = {.val = RESET_UNIT_L2CPU_RESET_REG_DEFAULT};
  l2cpu_reset.f.l2cpu_reset_n = 0xf;
  WriteReg(RESET_UNIT_L2CPU_RESET_REG_ADDR, l2cpu_reset.val);
}

// Assert soft reset for all RISC-V cores
// L2CPU is skipped due to JIRA issues BH-25 and BH-28
static void AssertSoftResets() {
  const uint8_t kNocRing = 0;
  const uint8_t kNocTlb = 0;
  const uint32_t kSoftReset0Addr = 0xFFB121B0; // NOC address in each tile
  const uint32_t kAllRiscSoftReset = 0x47800;

  // Broadcast to SOFT_RESET_0 of all Tensixes
  // Harvesting is handled by broadcast disables of NocInit
  NOC2AXITensixBroadcastTlbSetup(kNocRing, kNocTlb, kSoftReset0Addr, kNoc2AxiOrderingStrict);
  NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr, kAllRiscSoftReset);

  // TODO: Handle harvesting for ETH and GDDR (don't touch harvested tiles)
  // Broadcast to SOFT_RESET_0 of ETH
  for (uint8_t eth_inst = 0; eth_inst < 14; eth_inst++) {
    uint8_t x, y;
    GetEthNocCoords(eth_inst, kNocRing, &x, &y);
    NOC2AXITlbSetup(kNocRing, kNocTlb, x, y, kSoftReset0Addr);
    NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr, kAllRiscSoftReset);
  }

  // Broadcast to SOFT_RESET_0 of GDDR
  // Note that there are 3 NOC nodes for each GDDR instance
  for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
    for (uint8_t noc_node_inst = 0; noc_node_inst < 3; noc_node_inst++) {
      uint8_t x, y;
      GetGddrNocCoords(gddr_inst, noc_node_inst, kNocRing, &x, &y);
      NOC2AXITlbSetup(kNocRing, kNocTlb, x, y, kSoftReset0Addr);
      NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr, kAllRiscSoftReset);
    }
  }
}

// Deassert RISC reset from reset_unit for all RISC-V cores
// L2CPU is skipped due to JIRA issues BH-25 and BH-28
static void DeassertRiscvResets() {
  // TODO: Handle harvesting (keep RISC-V cores of harvested tiles in reset)

  for (uint32_t i = 0; i < 8; i++) {
    WriteReg(RESET_UNIT_TENSIX_RISC_RESET_0_REG_ADDR + i * 4, 0xffffffff);
  }

  RESET_UNIT_ETH_RESET_reg_u eth_reset;
  eth_reset.val = ReadReg(RESET_UNIT_ETH_RESET_REG_ADDR);
  eth_reset.f.eth_risc_reset_n = 0x3fff;
  WriteReg(RESET_UNIT_ETH_RESET_REG_ADDR, eth_reset.val);

  RESET_UNIT_DDR_RESET_reg_u ddr_reset;
  ddr_reset.val = ReadReg(RESET_UNIT_DDR_RESET_REG_ADDR);
  ddr_reset.f.ddr_risc_reset_n = 0xffffff;
  WriteReg(RESET_UNIT_DDR_RESET_REG_ADDR, ddr_reset.val);
}

static void InitResetInterrupt(uint8_t pcie_inst) {
  if (pcie_inst == 0) {
    IRQ_CONNECT(IRQNUM_PCIE0_ERR_INTR, 0, ChipResetRequest, IRQNUM_PCIE0_ERR_INTR, 0);
    irq_enable(IRQNUM_PCIE0_ERR_INTR);
  } else if (pcie_inst == 1) {
    IRQ_CONNECT(IRQNUM_PCIE1_ERR_INTR, 0, ChipResetRequest, IRQNUM_PCIE1_ERR_INTR, 0);
    irq_enable(IRQNUM_PCIE1_ERR_INTR);
  }
}

static void InitMrisc() {
  const char kMriscFwCfgTag[IMAGE_TAG_SIZE] = "memfwcfg";
  const char kMriscFwTag[IMAGE_TAG_SIZE] = "memfw";
  uint32_t fw_size = 0;

  for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
    for (uint8_t noc2axi_port = 0; noc2axi_port < 3; noc2axi_port++) {
      SetAxiEnable(gddr_inst, noc2axi_port, true);
    }
  }

  if (load_bin_by_tag(&boot_fs_data, kMriscFwTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle this more gracefully
    return;
  }

  for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
    LoadMriscFw(gddr_inst, large_sram_buffer, fw_size);
  }

  if (load_bin_by_tag(&boot_fs_data, kMriscFwCfgTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle this more gracefully
    return;
  }

  uint32_t gddr_speed = GetGddrSpeedFromCfg(large_sram_buffer);
  if (!IN_RANGE(gddr_speed,MIN_GDDR_SPEED,MAX_GDDR_SPEED)) {
    // Error
    // TODO: Handle this more gracefully
    return;
  }

  if (SetGddrMemClk(gddr_speed / GDDR_SPEED_TO_MEMCLK_RATIO)) {
    // Error
    // TODO: Handle this more gracefully
    return;
  }

  for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
    LoadMriscFwCfg(gddr_inst, large_sram_buffer, fw_size);
    ReleaseMriscReset(gddr_inst);
  }

  // TODO: Check for MRISC FW success / failure
}

static void SerdesEthInit() {
  uint32_t ring = 0;

  uint32_t load_serdes = BIT(2) | BIT(5); // Serdes 2, 5 are always for ETH
  // Select the other ETH Serdes instances based on pcie serdes properties
  if (get_fw_table()->pci0_property_table.pcie_mode == FwTable_PciPropertyTable_PcieMode_DISABLED) { // Enable Serdes 0, 1
    load_serdes |= BIT(0) | BIT(1);
  } else if (get_fw_table()->pci0_property_table.num_serdes == 1) { // Just enable Serdes 1
    load_serdes |= BIT(1);
  }
  if (get_fw_table()->pci1_property_table.pcie_mode == FwTable_PciPropertyTable_PcieMode_DISABLED) { // Enable Serdes 3, 4
    load_serdes |= BIT(3) | BIT(4);
  } else if (get_fw_table()->pci1_property_table.num_serdes == 1) { // Just enable Serdes 4
    load_serdes |= BIT(4);
  }

  // Load fw regs
  const char kSerdesEthFwRegsTag[IMAGE_TAG_SIZE] = "ethsdreg";
  uint32_t reg_table_size = 0;

  if (load_bin_by_tag(&boot_fs_data, kSerdesEthFwRegsTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &reg_table_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle more gracefully
    return;
  }

  for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
    if (load_serdes & (1 << serdes_inst)) {
      LoadSerdesEthRegs(serdes_inst, ring, (SerdesRegData *) large_sram_buffer, reg_table_size/sizeof(SerdesRegData));
    }
  }

  // Load fw
  const char kSerdesEthFwTag[IMAGE_TAG_SIZE] = "ethsdfw";
  uint32_t fw_size = 0;

  if (load_bin_by_tag(&boot_fs_data, kSerdesEthFwTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle more gracefully
    return;
  }

  for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
    if (load_serdes & (1 << serdes_inst)) {
      LoadSerdesEthFw(serdes_inst, ring, large_sram_buffer, fw_size);
    }
  }
}

static void EthInit() {
  uint32_t ring = 0;

  // Load fw
  const char kEthFwTag[IMAGE_TAG_SIZE] = "ethfw";
  uint32_t fw_size = 0;

  if (load_bin_by_tag(&boot_fs_data, kEthFwTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle more gracefully
    return;
  }

  for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
    LoadEthFw(eth_inst, ring, large_sram_buffer, fw_size);
  }

  // Load param table
  const char kEthFwCfgTag[IMAGE_TAG_SIZE] = "ethfwcfg";

  if (load_bin_by_tag(&boot_fs_data, kEthFwCfgTag, large_sram_buffer,
                      SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
    // Error
    // TODO: Handle more gracefully
    return;
  }

  for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
    LoadEthFwCfg(eth_inst, ring, large_sram_buffer, fw_size);
    ReleaseEthReset(eth_inst, ring);
  }
}

uint32_t InitFW() {
  WriteReg(STATUS_FW_VERSION_REG_ADDR, APPVERSION);

  // Initialize ARC DMA
  ArcDmaConfig();
  ArcDmaInitCh(0, 0, 15);

  // Initialize SPI EEPROM and the filesystem
  InitSpiFS();

  return 0;
}

// Returns 0 on success, non-zero on failure
uint32_t InitHW() {
  // Write a status register indicating HW init progress
  STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};
  boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
  boot_status0.f.hw_init_status = kHwInitStarted;
  WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP1);
  
  // Load FW config, Read Only and Flash Info tables from SPI filesystem
  // TODO: Add some kind of error handling if the load fails
  load_fw_table(large_sram_buffer, SCRATCHPAD_SIZE);
  load_read_only_table(large_sram_buffer, SCRATCHPAD_SIZE);
  load_flash_info_table(large_sram_buffer, SCRATCHPAD_SIZE);

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP2);
  // Enable CATMON for early thermal protection
  CATInit();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP3);
  // Put all PLLs back into bypass, since tile resets need to be deasserted at low speed
  PLLAllBypass();
  DeassertTileResets();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP4);
  // Init clocks to faster (but safe) levels
  PLLInit();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP5);
  // Enable Process + Voltage + Thermal monitors
  PVTInit();

  // Initialize NOC so we can broadcast to all Tensixes
  NocInit();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP6);
  // Assert Soft Reset for ERISC, MRISC Tensix (skip L2CPU due to bug)
  AssertSoftResets();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP7);
  // Go back to PLL bypass, since RISCV resets need to be deasserted at low speed
  PLLAllBypass();
  // Deassert RISC reset from reset_unit
  DeassertRiscvResets();
  PLLInit();

  // Initialize the serdes based on board type and asic location - data will be in fw_table
  // p100: PCIe1 x16
  // p150: PCIe0 x16
  // p300: Left (CPU1) PCIe1 x8, Right (CPU0) PCIe0 x8
  // BH UBB: PCIe1 x8
  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP8);
  if (get_fw_table()->pci0_property_table.pcie_mode != FwTable_PciPropertyTable_PcieMode_DISABLED &&
      PCIeInitOk == PCIeInit(0, &get_fw_table()->pci0_property_table)) {
    InitResetInterrupt(0);
  }

  if (get_fw_table()->pci1_property_table.pcie_mode != FwTable_PciPropertyTable_PcieMode_DISABLED &&
      PCIeInitOk == PCIeInit(1, &get_fw_table()->pci1_property_table)) {
    InitResetInterrupt(1);
  }

  // TODO: Load MRISC (DRAM RISC) FW to all DRAMs in the middle NOC node
  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP9);
  InitMrisc();

  // TODO: Load ERISC (Ethernet RISC) FW to all ethernets (8 of them)
  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPA);
  SerdesEthInit();
  EthInit();

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPB);
  InitSmbusTarget();

  // Initiate AVS interface and switch vout control to AVSBus
  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPC);
  AVSInit();
  SwitchVoutControl(AVSVoutCommand);

  SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPD);
  if (get_fw_table()->feature_enable.cg_en) {
    EnableTensixCG();
  }

  // Indicate successful HW Init
  boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
  boot_status0.f.hw_init_status = kHwInitDone;
  WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

  return 0;
}
