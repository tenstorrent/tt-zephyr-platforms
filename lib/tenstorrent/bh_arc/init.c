/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aiclk_ppm.h"
#include "avs.h"
#include "dvfs.h"
#include "eth.h"
#include "fan_ctrl.h"
#include "gddr.h"
#include "harvesting.h"
#include "init_common.h"
#include "noc.h"
#include "noc_init.h"
#include "pcie.h"
#include "pll.h"
#include "pvt.h"
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
#include <zephyr/drivers/misc/bh_fwtable.h>

LOG_MODULE_REGISTER(InitHW, CONFIG_TT_APP_LOG_LEVEL);

uint8_t large_sram_buffer[SCRATCHPAD_SIZE] __aligned(4);

/* Assert soft reset for all RISC-V cores */
/* L2CPU is skipped due to JIRA issues BH-25 and BH-28 */
static int AssertSoftResets(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP6);
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Assert Soft Reset for ERISC, MRISC Tensix (skip L2CPU due to bug) */

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

	return 0;
}
SYS_INIT(AssertSoftResets, APPLICATION, 10);

/* Deassert RISC reset from reset_unit for all RISC-V cores */
/* L2CPU is skipped due to JIRA issues BH-25 and BH-28 */
static int DeassertRiscvResets(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP7);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Go back to PLL bypass, since RISCV resets need to be deasserted at low speed */
	PLLAllBypass();

	/* Deassert RISC reset from reset_unit */

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

	PLLInit();

	return 0;
}
SYS_INIT(DeassertRiscvResets, APPLICATION, 11);

#ifndef CONFIG_TT_SMC_RECOVERY
static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

static uint8_t ToggleTensixReset(uint32_t msg_code, const struct request *req, struct response *rsp)
{
	/* Assert reset (active low) */
	RESET_UNIT_TENSIX_RESET_reg_u tensix_reset = {.val = 0};

	for (uint32_t i = 0; i < 8; i++) {
		WriteReg(RESET_UNIT_TENSIX_RESET_0_REG_ADDR + i * 4, tensix_reset.val);
	}

	/* Deassert reset */
	tensix_reset.val = 0xffffffff;
	for (uint32_t i = 0; i < 8; i++) {
		WriteReg(RESET_UNIT_TENSIX_RESET_0_REG_ADDR + i * 4, tensix_reset.val);
	}

	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_TOGGLE_TENSIX_RESET, ToggleTensixReset);

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
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->feature_enable.cg_en) {
		EnableTensixCG();
	}
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->feature_enable.noc_translation_en) {
		InitNocTranslationFromHarvesting();
	}

	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_REINIT_TENSIX, ReinitTensix);
#endif

static int bh_arc_init_start(void)
{
	/* Write a status register indicating HW init progress */
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};

	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitStarted;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP1);
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP2);

	return 0;
}
SYS_INIT(bh_arc_init_start, APPLICATION, 3);

int tt_init_status;
STATUS_ERROR_STATUS0_reg_u error_status0;

static int bh_arc_init_end(void)
{
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};

	/* Indicate successful HW Init */
	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	/* Record FW ID */
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		boot_status0.f.fw_id = FW_ID_SMC_RECOVERY;
	} else {
		boot_status0.f.fw_id = FW_ID_SMC_NORMAL;
	}
	boot_status0.f.hw_init_status = (tt_init_status == 0) ? kHwInitDone : kHwInitError;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);
	WriteReg(STATUS_ERROR_STATUS0_REG_ADDR, error_status0.val);

	return 0;
}
SYS_INIT(bh_arc_init_end, APPLICATION, 22);
