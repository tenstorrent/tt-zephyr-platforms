/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aiclk_ppm.h"
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
#include "timer.h"

#include <stdint.h>

#include <tenstorrent/msgqueue.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/post_code.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(InitHW, CONFIG_TT_APP_LOG_LEVEL);

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
	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
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

static int CheckGddrTraining(uint8_t gddr_inst, k_timepoint_t timeout)
{
	uint32_t poll_val;
	bool timedout = false;

	while (true) {
		poll_val = MriscRegRead32(gddr_inst, MRISC_INIT_STATUS);
		if (poll_val == MRISC_INIT_FINISHED || poll_val == MRISC_INIT_FAILED) {
			break;
		}
		if (sys_timepoint_expired(timeout)) {
			timedout = true;
			break;
		}
		k_msleep(1);
	}
	if (poll_val == MRISC_INIT_FINISHED) {
		return EXIT_SUCCESS;
	}
	uint32_t post_code = MriscRegRead32(gddr_inst, MRISC_POST_CODE);

	if (timedout) {
		LOG_ERR("Timeout after %d ms waiting for GDDR instance %d to "
			"initialize. Post code: 0x%x\n",
			MRISC_INIT_TIMEOUT, gddr_inst, post_code);
		return -ETIMEDOUT;
	}
	LOG_ERR("GDDR instance %d failed to initialize. Post code: 0x%x\n",
		gddr_inst, post_code);
	return -EIO;
}

static int CheckGddrHwTest(void)
{
	/* First kick off all tests in parallel, then check their results. Test will take
	 * approximately 300-400 ms.
	 */
	uint8_t test_started = 0; /* Bitmask of tests started */
	int any_error = 0;

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(tile_enable.gddr_enabled, gddr_inst)) {
			int error = StartHwMemtest(gddr_inst, 26, 0, 0);

			if (error == -ENOTSUP) {
				/* Shouldn't be considered a test failure if MRISC FW is too old. */
				LOG_WRN("GDDR %d MRISC FW version does not support memtest. "
					"Skipping the test on this instance.\n",
					gddr_inst);
			} else if (error < 0) {
				LOG_WRN("Failed to start GDDR %d memory test. Got error code %d.\n",
					gddr_inst, error);
				any_error = -EIO;
			} else {
				test_started |= BIT(gddr_inst);
			}
		}
	}
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(MRISC_MEMTEST_TIMEOUT));

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(test_started, gddr_inst)) {
			int error = CheckHwMemtestResult(gddr_inst, timeout);

			if (error < 0) {
				any_error = -EIO;
				if (error == -ETIMEDOUT) {
					LOG_ERR("GDDR %d memory test timed out.\n", gddr_inst);
				} else if (error == -EIO) {
					LOG_ERR("GDDR %d memory test failed comparison.\n",
						gddr_inst);
				} else {
					LOG_ERR("GDDR %d memory test failed with error code %d.\n",
						gddr_inst, error);
				}
			} else {
				LOG_DBG("GDDR %d memory test passed.\n", gddr_inst);
			}
		}
	}
	return any_error;
}

static int InitMrisc(void)
{
	static const char kMriscFwCfgTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "memfwcfg";
	static const char kMriscFwTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "memfw";
	size_t fw_size = 0;

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		for (uint8_t noc2axi_port = 0; noc2axi_port < 3; noc2axi_port++) {
			SetAxiEnable(gddr_inst, noc2axi_port, true);
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, kMriscFwTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		LOG_ERR("Failed to load MRISC FW from file system to ARC.\n");
		return -EIO;
	}
	uint32_t dram_mask = GetDramMask();

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			if (LoadMriscFw(gddr_inst, large_sram_buffer, fw_size)) {
				LOG_ERR("Failed to load MRISC FW to MRISC from ARC."
					"Failed on GDDR instance %d.\n",
					gddr_inst);
				return -EIO;
			}
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, kMriscFwCfgTag, large_sram_buffer, SCRATCHPAD_SIZE,
				&fw_size) != TT_BOOT_FS_OK) {
		LOG_ERR("Failed to load MRISC FW config from file system to ARC.\n");
		return -EIO;
	}

	uint32_t gddr_speed = GetGddrSpeedFromCfg(large_sram_buffer);

	if (!IN_RANGE(gddr_speed, MIN_GDDR_SPEED, MAX_GDDR_SPEED)) {
		LOG_WRN("Invalid GDDR speed in FW config: %d Mbps\n"
			"Must be between %d Mbps and %d Mbps\n"
			"Setting to minimum speed %d Mbps\n",
			gddr_speed, MIN_GDDR_SPEED, MAX_GDDR_SPEED, MIN_GDDR_SPEED);
		gddr_speed = MIN_GDDR_SPEED;
	}

	if (SetGddrMemClk(gddr_speed / GDDR_SPEED_TO_MEMCLK_RATIO)) {
		LOG_ERR("Failed to set GDDR memory clock to requested: %d Mbps\n", gddr_speed);
		return -EIO;
	}

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			if (LoadMriscFwCfg(gddr_inst, large_sram_buffer, fw_size)) {
				LOG_ERR("Failed to load MRISC FW config to MRISC from ARC."
					"Failed on GDDR instance %d.\n",
					gddr_inst);
				return -EIO;
			}
			MriscRegWrite32(gddr_inst, MRISC_INIT_STATUS, MRISC_INIT_BEFORE);
			ReleaseMriscReset(gddr_inst);
		}
	}

	return EXIT_SUCCESS;
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

#ifndef CONFIG_TT_SMC_RECOVERY
/**
 * @brief Redo Tensix init that gets cleared on Tensix reset
 *
 * This includes all NOC programming and any programming within the tile.
 */
static uint8_t ReinitTensix(uint32_t msg_code, const struct request *req, struct response *rsp)
{
	ClearNocTranslation();
	/* We technically don't have to re-program the entire NOC (only the Tensix NOC portions),
	 * but it's simpler to reuse the same functions to re-program all of it.
	 */
	NocInit();
	if (get_fw_table()->feature_enable.cg_en) {
		EnableTensixCG();
	}
	if (get_fw_table()->feature_enable.noc_translation_en) {
		InitNocTranslationFromHarvesting();
	}

	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_REINIT_TENSIX, ReinitTensix);
#endif

#ifdef CONFIG_TT_BH_ARC_SYSINIT
static int InitHW(void)
{
	/* Write a status register indicating HW init progress */
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};
	STATUS_ERROR_STATUS0_reg_u error_status0 = {0};

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
		/* Initialize some AICLK tracking variables */
		InitAiclkPPM();
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

	WriteReg(PCIE_INIT_CPL_TIME_REG_ADDR, TimerTimestamp());

	/* Load MRISC (DRAM RISC) FW to all DRAMs in the middle NOC node */
	bool init_errors = false;
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP9);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		if (InitMrisc()) {
			LOG_ERR("Failed to initialize GDDR.\n");
			init_errors = true;
		}
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
		if (RegulatorInit(get_pcb_type())) {
			LOG_ERR("Failed to initialize regulators.\n");
			error_status0.f.regulator_init_error = 1;
			init_errors = true;
		}
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

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPE);
	/* Check GDDR training status. */
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(MRISC_INIT_TIMEOUT));

		for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
			if (IS_BIT_SET(GetDramMask(), gddr_inst)) {
				int error = CheckGddrTraining(gddr_inst, timeout);

				if (error == -ETIMEDOUT) {
					LOG_ERR("GDDR instance %d timed out during training.\n",
						gddr_inst);
					init_errors = true;
				} else if (error) {
					LOG_ERR("GDDR instance %d failed training.\n", gddr_inst);
					init_errors = true;
				}
			}
		}
	}
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		if (!init_errors) {
			if (CheckGddrHwTest() < 0) {
				LOG_ERR("GDDR HW test failed.\n");
				init_errors = true;
			}
		}
	}

	/* Indicate successful HW Init */
	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	/* Record FW ID */
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		boot_status0.f.fw_id = FW_ID_SMC_RECOVERY;
	} else {
		boot_status0.f.fw_id = FW_ID_SMC_NORMAL;
	}
	boot_status0.f.hw_init_status = init_errors ? kHwInitError : kHwInitDone;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);
	WriteReg(STATUS_ERROR_STATUS0_REG_ADDR, error_status0.val);

	return 0;
}
SYS_INIT(InitHW, APPLICATION, 99);
#endif /* CONFIG_TT_BH_ARC_SYSINIT */
