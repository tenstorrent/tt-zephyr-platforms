/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "avs.h"
#include "cat.h"
#include "dvfs.h"
#include "eth.h"
#include "fan_ctrl.h"
#include "flash_info_table.h"
#include "fw_table.h"
#include "gddr.h"
#include "harvesting.h"
#include "init_common.h"
#include "noc.h"
#include "noc_init.h"
#include "pcie.h"
#include "pll.h"
#include "pvt.h"
#include "read_only_table.h"
#include "reg.h"
#include "regulator.h"
#include "serdes_eth.h"
#include "smbus_target.h"
#include "status_reg.h"
#include "telemetry.h"
#include "telemetry_internal.h"
#include "tensix_cg.h"

#include <stdint.h>

#include <tenstorrent/msgqueue.h>
#include <tenstorrent/post_code.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

static uint8_t large_sram_buffer[SCRATCHPAD_SIZE] __aligned(4);

/* Assert soft reset for all RISC-V cores */
/* L2CPU is skipped due to JIRA issues BH-25 and BH-28 */
static void AssertSoftResets(void)
{
	const uint8_t kNocRing = 0;
	const uint8_t kNocTlb = 0;
	const uint32_t kSoftReset0Addr = 0xFFB121B0; /* NOC address in each tile */
	const uint32_t kAllRiscSoftReset = 0x47800;

	/* Broadcast to SOFT_RESET_0 of all Tensixes */
	/* Harvesting is handled by broadcast disables of NocInit */
	NOC2AXITensixBroadcastTlbSetup(kNocRing, kNocTlb, kSoftReset0Addr, kNoc2AxiOrderingStrict);
	NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr, kAllRiscSoftReset);

	/* Write to SOFT_RESET_0 of ETH */
	for (uint8_t eth_inst = 0; eth_inst < 14; eth_inst++) {
		uint8_t x, y;

		/* Skip harvested ETH tiles */
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			GetEthNocCoords(eth_inst, kNocRing, &x, &y);
			NOC2AXITlbSetup(kNocRing, kNocTlb, x, y, kSoftReset0Addr);
			NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr, kAllRiscSoftReset);
		}
	}

	/* Write to SOFT_RESET_0 of GDDR */
	/* Note that there are 3 NOC nodes for each GDDR instance */
	for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
		/* Skip harvested GDDR tiles */
		if (tile_enable.gddr_enabled & BIT(gddr_inst)) {
			for (uint8_t noc_node_inst = 0; noc_node_inst < 3; noc_node_inst++) {
				uint8_t x, y;

				GetGddrNocCoords(gddr_inst, noc_node_inst, kNocRing, &x, &y);
				NOC2AXITlbSetup(kNocRing, kNocTlb, x, y, kSoftReset0Addr);
				NOC2AXIWrite32(kNocRing, kNocTlb, kSoftReset0Addr,
					kAllRiscSoftReset);
			}
		}
	}
}

/* Deassert RISC reset from reset_unit for all RISC-V cores */
/* L2CPU is skipped due to JIRA issues BH-25 and BH-28 */
static void DeassertRiscvResets(void)
{
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

static void InitMrisc(void)
{
	static const char kMriscFwCfgTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "memfwcfg";
	static const char kMriscFwTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "memfw";
	size_t fw_size = 0;

	for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
		for (uint8_t noc2axi_port = 0; noc2axi_port < 3; noc2axi_port++) {
			SetAxiEnable(gddr_inst, noc2axi_port, true);
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, kMriscFwTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle this more gracefully */
		return;
	}
	uint32_t dram_mask = tile_enable.gddr_enabled; /* bit mask */

	if (get_fw_table()->has_dram_table && get_fw_table()->dram_table.dram_mask_en) {
		dram_mask &= get_fw_table()->dram_table.dram_mask;
	}
	for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
		if ((dram_mask >> gddr_inst) & 1) {
			LoadMriscFw(gddr_inst, large_sram_buffer, fw_size);
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, kMriscFwCfgTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle this more gracefully */
		return;
	}

	uint32_t gddr_speed = GetGddrSpeedFromCfg(large_sram_buffer);

	if (!IN_RANGE(gddr_speed, MIN_GDDR_SPEED, MAX_GDDR_SPEED)) {
		/* Error */
		/* TODO: Handle this more gracefully */
		return;
	}

	if (SetGddrMemClk(gddr_speed / GDDR_SPEED_TO_MEMCLK_RATIO)) {
		/* Error */
		/* TODO: Handle this more gracefully */
		return;
	}

	for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
		if ((dram_mask >> gddr_inst) & 1) {
			LoadMriscFwCfg(gddr_inst, large_sram_buffer, fw_size);
			ReleaseMriscReset(gddr_inst);
		}
	}

	/* TODO: Check for MRISC FW success / failure */
}

static void SerdesEthInit(void)
{
	uint32_t ring = 0;

	SetupEthSerdesMux(tile_enable.eth_enabled);

	uint32_t load_serdes = BIT(2) | BIT(5); /* Serdes 2, 5 are always for ETH */
	/* Select the other ETH Serdes instances based on pcie serdes properties */
	if (get_fw_table()->pci0_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable Serdes 0, 1 */
		load_serdes |= BIT(0) | BIT(1);
	} else if (get_fw_table()->pci0_property_table.num_serdes == 1) { /* Just enable Serdes 1 */
		load_serdes |= BIT(1);
	}
	if (get_fw_table()->pci1_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable Serdes 3, 4 */
		load_serdes |= BIT(3) | BIT(4);
	} else if (get_fw_table()->pci1_property_table.num_serdes == 1) { /* Just enable Serdes 4 */
		load_serdes |= BIT(4);
	}

	/* Load fw regs */
	static const char kSerdesEthFwRegsTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "ethsdreg";
	uint32_t reg_table_size = 0;

	if (tt_boot_fs_get_file(&boot_fs_data, kSerdesEthFwRegsTag, large_sram_buffer,
				SCRATCHPAD_SIZE, &reg_table_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return;
	}

	for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
		if (load_serdes & (1 << serdes_inst)) {
			LoadSerdesEthRegs(serdes_inst, ring, (SerdesRegData *)large_sram_buffer,
					  reg_table_size / sizeof(SerdesRegData));
		}
	}

	/* Load fw */
	static const char kSerdesEthFwTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "ethsdfw";
	size_t fw_size = 0;

	if (tt_boot_fs_get_file(&boot_fs_data, kSerdesEthFwTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return;
	}

	for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
		if (load_serdes & (1 << serdes_inst)) {
			LoadSerdesEthFw(serdes_inst, ring, large_sram_buffer, fw_size);
		}
	}
}

static void EthInit(void)
{
	uint32_t ring = 0;

	/* Early exit if no ETH tiles enabled */
	if (tile_enable.eth_enabled == 0) {
		return;
	}

	/* Load fw */
	static const char kEthFwTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "ethfw";
	size_t fw_size = 0;

	if (tt_boot_fs_get_file(&boot_fs_data, kEthFwTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return;
	}

	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			LoadEthFw(eth_inst, ring, large_sram_buffer, fw_size);
		}
	}

	/* Load param table */
	static const char kEthFwCfgTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "ethfwcfg";

	if (tt_boot_fs_get_file(&boot_fs_data, kEthFwCfgTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return;
	}

	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			LoadEthFwCfg(eth_inst, ring, tile_enable.eth_enabled,
				large_sram_buffer, fw_size);
			ReleaseEthReset(eth_inst, ring);
		}
	}
}

#ifdef CONFIG_TT_BH_ARC_SYSINIT
static int InitHW(void)
{
	/* Write a status register indicating HW init progress */
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};

	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitStarted;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP1);

	/* Load FW config, Read Only and Flash Info tables from SPI filesystem */
	/* TODO: Add some kind of error handling if the load fails */
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		load_fw_table(large_sram_buffer, SCRATCHPAD_SIZE);
	}
	load_read_only_table(large_sram_buffer, SCRATCHPAD_SIZE);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		load_flash_info_table(large_sram_buffer, SCRATCHPAD_SIZE);
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP2);
	/* Enable CATMON for early thermal protection */
	CATInit();

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		CalculateHarvesting();
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP3);
	/* Put all PLLs back into bypass, since tile resets need to be deasserted at low speed */
	PLLAllBypass();
	DeassertTileResets();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP4);
	/* Init clocks to faster (but safe) levels */
	PLLInit();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP5);

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		/* Enable Process + Voltage + Thermal monitors */
		PVTInit();

		/* Initialize NOC so we can broadcast to all Tensixes */
		NocInit();
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP6);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		/* Assert Soft Reset for ERISC, MRISC Tensix (skip L2CPU due to bug) */
		AssertSoftResets();
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP7);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		/* Go back to PLL bypass, since RISCV resets need to be deasserted at low speed */
		PLLAllBypass();
		/* Deassert RISC reset from reset_unit */
		DeassertRiscvResets();
		PLLInit();
	}

	/* Initialize the serdes based on board type and asic location - data will be in fw_table */
	/* p100: PCIe1 x16 */
	/* p150: PCIe0 x16 */
	/* p300: Left (CPU1) PCIe1 x8, Right (CPU0) PCIe0 x8 */
	/* BH UBB: PCIe1 x8 */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP8);

	FwTable_PciPropertyTable pci0_property_table;
	FwTable_PciPropertyTable pci1_property_table;

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		pci0_property_table = (FwTable_PciPropertyTable){
			.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP,
			.num_serdes = 2,
		};
		pci1_property_table = (FwTable_PciPropertyTable){
			.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP,
			.num_serdes = 2,
		};
	} else {
		pci0_property_table = get_fw_table()->pci0_property_table;
		pci1_property_table = get_fw_table()->pci1_property_table;
	}

	if ((pci0_property_table.pcie_mode != FwTable_PciPropertyTable_PcieMode_DISABLED) &&
	    (PCIeInitOk == PCIeInit(0, &pci0_property_table))) {
		InitResetInterrupt(0);
	}

	if ((pci1_property_table.pcie_mode != FwTable_PciPropertyTable_PcieMode_DISABLED) &&
	    (PCIeInitOk == PCIeInit(1, &pci1_property_table))) {
		InitResetInterrupt(1);
	}

	/* TODO: Load MRISC (DRAM RISC) FW to all DRAMs in the middle NOC node */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP9);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		InitMrisc();
	}

	/* TODO: Load ERISC (Ethernet RISC) FW to all ethernets (8 of them) */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPA);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		SerdesEthInit();
		EthInit();
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPB);
	InitSmbusTarget();

	/* Initiate AVS interface and switch vout control to AVSBus */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPC);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		RegulatorInit(get_pcb_type());
		AVSInit();
		SwitchVoutControl(AVSVoutCommand);
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPD);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		if (get_fw_table()->feature_enable.cg_en) {
			EnableTensixCG();
		}

		if (get_fw_table()->feature_enable.noc_translation_en) {
			InitNocTranslationFromHarvesting();
		}
	}

	/* Indicate successful HW Init */
	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitDone;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	return 0;
}
SYS_INIT(InitHW, APPLICATION, 99);
#endif /* CONFIG_TT_BH_ARC_SYSINIT */
