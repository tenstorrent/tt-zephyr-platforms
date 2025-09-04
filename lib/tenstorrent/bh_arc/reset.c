/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cm2dm_msg.h"
#include "eth.h"
#include "harvesting.h"
#include "init.h"
#include "irqnum.h"
#include "noc.h"
#include "noc_init.h"
#include "noc2axi.h"
#include "reg.h"
#include "status_reg.h"
#include "tensix_cg.h"

#include <stdint.h>

#include <tenstorrent/msgqueue.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/post_code.h>
#include <tenstorrent/sys_init_defines.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/memc/memc_tt_bh.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>

#define DT_DRV_COMPAT         tenstorrent_bh_clock_control
#define PLL_DEVICE_INIT(inst) DEVICE_DT_INST_GET(inst),

static const struct device *const pll_devs[] = {DT_INST_FOREACH_STATUS_OKAY(PLL_DEVICE_INIT)};

LOG_MODULE_REGISTER(InitHW, CONFIG_TT_APP_LOG_LEVEL);

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));
STATUS_ERROR_STATUS0_reg_u error_status0;

static const struct device *memc_devices[] = {DT_INST_FOREACH_STATUS_OKAY(MEMC_TT_BH_DEVICE_GET)};

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
	ARRAY_FOR_EACH_PTR(memc_devices, devp) {
		const struct device *dev = *devp;
		int gddr_inst = memc_tt_bh_inst_get(dev);

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
SYS_INIT_APP(AssertSoftResets);

/* Deassert RISC reset from reset_unit for all RISC-V cores */
/* L2CPU is skipped due to JIRA issues BH-25 and BH-28 */
static int DeassertRiscvResets(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP7);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Go back to PLL bypass, since RISCV resets need to be deasserted at low speed */
	ARRAY_FOR_EACH(pll_devs, i) {
		clock_control_configure(pll_devs[i], NULL,
					(void *)CLOCK_CONTROL_TT_BH_CONFIG_BYPASS);
	}
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

	ARRAY_FOR_EACH(pll_devs, i) {
		clock_control_set_rate(pll_devs[i],
				       (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_INIT_STATE,
				       (clock_control_subsys_rate_t)-1);
	};

	return 0;
}
SYS_INIT_APP(DeassertRiscvResets);

static __maybe_unused uint8_t ToggleTensixReset(uint32_t msg_code, const struct request *req,
						struct response *rsp)
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

#ifndef CONFIG_TT_SMC_RECOVERY
REGISTER_MESSAGE(MSG_TYPE_TOGGLE_TENSIX_RESET, ToggleTensixReset);
#endif

/**
 * @brief Redo Tensix init that gets cleared on Tensix reset
 *
 * This includes all NOC programming and any programming within the tile.
 */
static __maybe_unused uint8_t ReinitTensix(uint32_t msg_code, const struct request *req,
					   struct response *rsp)
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
#ifndef CONFIG_TT_SMC_RECOVERY
REGISTER_MESSAGE(MSG_TYPE_REINIT_TENSIX, ReinitTensix);
#endif

static int DeassertTileResets(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP3);

	if (!IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Put all PLLs back into bypass, since tile resets need to be deasserted at low speed */
	ARRAY_FOR_EACH(pll_devs, i) {
		clock_control_configure(pll_devs[i], NULL,
					(void *)CLOCK_CONTROL_TT_BH_CONFIG_BYPASS);
	}

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
	/* There are 8 instances of these tensix reset registers */
	for (uint32_t i = 0; i < 8; i++) {
		WriteReg(RESET_UNIT_TENSIX_RESET_0_REG_ADDR + i * 4, tensix_reset.val);
	}

	RESET_UNIT_DDR_RESET_reg_u ddr_reset = {.val = RESET_UNIT_DDR_RESET_REG_DEFAULT};

	ddr_reset.f.ddr_reset_n = 0xff;
	WriteReg(RESET_UNIT_DDR_RESET_REG_ADDR, ddr_reset.val);

	RESET_UNIT_L2CPU_RESET_reg_u l2cpu_reset = {.val = RESET_UNIT_L2CPU_RESET_REG_DEFAULT};

	l2cpu_reset.f.l2cpu_reset_n = 0xf;
	WriteReg(RESET_UNIT_L2CPU_RESET_REG_ADDR, l2cpu_reset.val);

	return 0;
}
SYS_INIT_APP(DeassertTileResets);
