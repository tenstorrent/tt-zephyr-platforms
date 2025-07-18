/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arc_dma.h"
#include "gddr.h"
#include "harvesting.h"
#include "init.h"
#include "noc.h"
#include "noc2axi.h"
#include "pll.h"
#include "reg.h"

#include <tenstorrent/post_code.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

/* This is the noc2axi instance we want to run the MRISC FW on */
#define MRISC_FW_NOC2AXI_PORT 0
#define MRISC_SETUP_TLB       13
#define MRISC_L1_ADDR         (1ULL << 37)
#define MRISC_REG_ADDR        (1ULL << 40)
#define MRISC_FW_CFG_OFFSET   0x3C00

#define MRISC_FW_TAG     "memfw"
#define MRISC_FW_CFG_TAG "memfwcfg"

LOG_MODULE_REGISTER(gddr, CONFIG_TT_APP_LOG_LEVEL);

extern uint8_t large_sram_buffer[SCRATCHPAD_SIZE] __aligned(4);

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

volatile void *SetupMriscL1Tlb(uint8_t gddr_inst)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	return GetTlbWindowAddr(0, MRISC_SETUP_TLB, MRISC_L1_ADDR);
}

uint32_t MriscL1Read32(uint8_t gddr_inst, uint32_t addr)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	return NOC2AXIRead32(0, MRISC_SETUP_TLB, MRISC_L1_ADDR + addr);
}

void MriscL1Write32(uint8_t gddr_inst, uint32_t addr, uint32_t val)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	NOC2AXIWrite32(0, MRISC_SETUP_TLB, MRISC_L1_ADDR + addr, val);
}

uint32_t MriscRegRead32(uint8_t gddr_inst, uint32_t addr)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_REG_ADDR + addr);
	return NOC2AXIRead32(0, MRISC_SETUP_TLB, MRISC_REG_ADDR + addr);
}

void MriscRegWrite32(uint8_t gddr_inst, uint32_t addr, uint32_t val)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_REG_ADDR + addr);
	NOC2AXIWrite32(0, MRISC_SETUP_TLB, MRISC_REG_ADDR + addr, val);
}

int read_gddr_telemetry_table(uint8_t gddr_inst, gddr_telemetry_table_t *gddr_telemetry)
{
	volatile uint8_t *mrisc_l1 = SetupMriscL1Tlb(gddr_inst);
	bool dma_pass = ArcDmaTransfer((const void *) (mrisc_l1 + GDDR_TELEMETRY_TABLE_ADDR),
		gddr_telemetry, sizeof(*gddr_telemetry));
	if (!dma_pass) {
		/* If DMA failed, can read 32b at a time via NOC2AXI */
		for (int i = 0; i < sizeof(*gddr_telemetry) / 4; i++) {
			((uint32_t *)gddr_telemetry)[i] =
				MriscL1Read32(gddr_inst, GDDR_TELEMETRY_TABLE_ADDR + i * 4);
		}
	}
	/* Check that version matches expectation. */
	if (gddr_telemetry->telemetry_table_version != GDDR_TELEMETRY_TABLE_T_VERSION) {
		LOG_WRN_ONCE("GDDR telemetry table version mismatch: %d (expected %d)",
			     gddr_telemetry->telemetry_table_version,
			     GDDR_TELEMETRY_TABLE_T_VERSION);
		return -ENOTSUP;
	}
	return 0;
}

void ReleaseMriscReset(uint8_t gddr_inst)
{
	const uint32_t kSoftReset0Addr = 0xFFB121B0;
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, kSoftReset0Addr);

	volatile uint32_t *soft_reset_0 = GetTlbWindowAddr(0, MRISC_SETUP_TLB, kSoftReset0Addr);
	*soft_reset_0 &= ~(1 << 11); /* Clear bit corresponding to MRISC reset */
}

void SetAxiEnable(uint8_t gddr_inst, uint8_t noc2axi_port, bool axi_enable)
{
	const uint32_t kNiuCfg0Addr[NUM_NOCS] = {0xFFB20100, 0xFFB30100};
	uint8_t x, y;
	volatile uint32_t *niu_cfg_0[NUM_NOCS];

	for (uint8_t i = 0; i < NUM_NOCS; i++) {
		GetGddrNocCoords(gddr_inst, noc2axi_port, i, &x, &y);
		/* Note this actually sets up two TLBs (one for each NOC) */
		NOC2AXITlbSetup(i, MRISC_SETUP_TLB, x, y, kNiuCfg0Addr[i]);
		niu_cfg_0[i] = GetTlbWindowAddr(i, MRISC_SETUP_TLB, kNiuCfg0Addr[i]);
	}

	if (axi_enable) {
		for (uint8_t i = 0; i < NUM_NOCS; i++) {
			*niu_cfg_0[i] |= (1 << NIU_CFG_0_AXI_SLAVE_ENABLE);
		}
	} else {
		for (uint8_t i = 0; i < NUM_NOCS; i++) {
			*niu_cfg_0[i] &= ~(1 << NIU_CFG_0_AXI_SLAVE_ENABLE);
		}
	}
}

int LoadMriscFw(uint8_t gddr_inst, uint8_t *fw_image, uint32_t fw_size)
{
	volatile uint32_t *mrisc_l1 = SetupMriscL1Tlb(gddr_inst);

	bool dma_pass = ArcDmaTransfer(fw_image, (void *)mrisc_l1, fw_size);

	return dma_pass ? 0 : -1;
}

int LoadMriscFwCfg(uint8_t gddr_inst, uint8_t *fw_cfg_image, uint32_t fw_cfg_size)
{
	volatile uint32_t *mrisc_l1 = SetupMriscL1Tlb(gddr_inst);

	bool dma_pass = ArcDmaTransfer(fw_cfg_image, (uint8_t *)mrisc_l1 + MRISC_FW_CFG_OFFSET,
				       fw_cfg_size);
	return dma_pass ? 0 : -1;
}

uint32_t GetDramMask(void)
{
	uint32_t dram_mask = tile_enable.gddr_enabled; /* bit mask */

	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->has_dram_table &&
	    tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask_en) {
		dram_mask &= tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask;
	}
	return dram_mask;
}

int StartHwMemtest(uint8_t gddr_inst, uint32_t addr_bits, uint32_t start_addr, uint32_t mask)
{
	uint32_t msg_args[3] = {addr_bits, start_addr, mask};

	/* Only run if MRISC FW support it. Must be > 2.6 */
	gddr_telemetry_table_t gddr_telemetry;

	if (read_gddr_telemetry_table(gddr_inst, &gddr_telemetry) < 0) {
		LOG_WRN("Failed to read GDDR telemetry table while starting memtest");
		return -ENOTSUP;
	}
	if (gddr_telemetry.mrisc_fw_version_major < 2 ||
	    (gddr_telemetry.mrisc_fw_version_major == 2 &&
	     gddr_telemetry.mrisc_fw_version_minor < 7)) {
		LOG_WRN("GDDR %d MRISC FW version %d.%d does not support memtest", gddr_inst,
			gddr_telemetry.mrisc_fw_version_major,
			gddr_telemetry.mrisc_fw_version_minor);
		return -ENOTSUP;
	}

	/*
	 * Messaging should not be done concurrently to the same GDDR instance, but still do sanity
	 * check if the message buffer is free.
	 */
	uint32_t status = MriscRegRead32(gddr_inst, MRISC_MSG_REGISTER);

	if (status != MRISC_MSG_TYPE_NONE) {
		LOG_WRN("GDDR %d message buffer is not free. Current value: 0x%x", gddr_inst,
			status);
		return -EBUSY;
	}
	if (addr_bits > 26) {
		LOG_WRN("Invalid number of address bits for memory test. Expected <= 26, got %d",
			addr_bits);
		return -EINVAL;
	}
	for (int i = 0; i < 3; i++) {
		MriscL1Write32(gddr_inst, GDDR_MSG_STRUCT_ADDR + i * 4, msg_args[i]);
	}
	MriscRegWrite32(gddr_inst, MRISC_MSG_REGISTER, MRISC_MSG_TYPE_RUN_MEMTEST);
	return 0;
}

int CheckHwMemtestResult(uint8_t gddr_inst, k_timepoint_t timeout)
{
	/* This should only be called after StartHwMemtest() has already been called. */
	while (MriscRegRead32(gddr_inst, MRISC_MSG_REGISTER) != 0) {
		/* Wait for the message to be processed */
		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("Timeout after %d ms waiting for GDDR instance %d to run "
				"memtest",
				MRISC_MEMTEST_TIMEOUT, gddr_inst);
			return -ETIMEDOUT;
		}
	}
	uint32_t pass = MriscL1Read32(gddr_inst, GDDR_MSG_STRUCT_ADDR + 8 * 4);

	if (pass != 0) {
		LOG_ERR("GDDR %d memory test failed", gddr_inst);
		return -EIO;
	}
	LOG_DBG("GDDR %d memory test passed", gddr_inst);
	return 0;
}

static int InitMrisc(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP9);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Load MRISC (DRAM RISC) FW to all DRAMs in the middle NOC node */

	size_t fw_size = 0;

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		for (uint8_t noc2axi_port = 0; noc2axi_port < 3; noc2axi_port++) {
			SetAxiEnable(gddr_inst, noc2axi_port, true);
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, MRISC_FW_TAG, (uint8_t *)large_sram_buffer,
				SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_get_file", MRISC_FW_TAG, -EIO);
		return -EIO;
	}
	uint32_t dram_mask = GetDramMask();

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			if (LoadMriscFw(gddr_inst, (uint8_t *)large_sram_buffer, fw_size)) {
				LOG_ERR("%s(%d) failed: %d", "LoadMriscFw", gddr_inst, -EIO);
				return -EIO;
			}
		}
	}

	if (tt_boot_fs_get_file(&boot_fs_data, MRISC_FW_CFG_TAG, (uint8_t *)large_sram_buffer,
				SCRATCHPAD_SIZE, &fw_size) != TT_BOOT_FS_OK) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_get_file", MRISC_FW_CFG_TAG, -EIO);
		return -EIO;
	}

	uint32_t gddr_speed = GetGddrSpeedFromCfg((uint8_t *)large_sram_buffer);

	if (!IN_RANGE(gddr_speed, MIN_GDDR_SPEED, MAX_GDDR_SPEED)) {
		LOG_WRN("%s() failed: %d", "GetGddrSpeedFromCfg", gddr_speed);
		gddr_speed = MIN_GDDR_SPEED;
	}

	if (SetGddrMemClk(gddr_speed / GDDR_SPEED_TO_MEMCLK_RATIO)) {
		LOG_ERR("%s(%d) failed: %d", "SetGddrMemClk", gddr_speed, -EIO);
		return -EIO;
	}

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			if (LoadMriscFwCfg(gddr_inst, (uint8_t *)large_sram_buffer, fw_size)) {
				LOG_ERR("%s(%d) failed: %d", "LoadMriscFwCfg", gddr_inst, -EIO);
				return -EIO;
			}
			MriscRegWrite32(gddr_inst, MRISC_INIT_STATUS, MRISC_INIT_BEFORE);
			ReleaseMriscReset(gddr_inst);
		}
	}

	return 0;
}
SYS_INIT(InitMrisc, APPLICATION, 14);

static int CheckGddrTraining(uint8_t gddr_inst, k_timepoint_t timeout)
{
	do {
		uint32_t poll_val = MriscRegRead32(gddr_inst, MRISC_INIT_STATUS);

		if (poll_val == MRISC_INIT_FINISHED) {
			return 0;
		}
		if (poll_val == MRISC_INIT_FAILED) {
			LOG_ERR("%s[%d]: 0x%x", "MRISC_INIT_STATUS", gddr_inst, poll_val);
			return -EIO;
		}
		k_msleep(1);
	} while (!sys_timepoint_expired(timeout));

	LOG_ERR("%s[%d]: 0x%x", "MRISC_POST_CODE", gddr_inst,
		MriscRegRead32(gddr_inst, MRISC_POST_CODE));

	return -ETIMEDOUT;
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
				LOG_DBG("%s(%d) %s: %d", "StartHwMemtest", gddr_inst, "skipped",
					error);
			} else if (error < 0) {
				LOG_ERR("%s(%d) %s: %d", "StartHwMemtest", gddr_inst, "failed",
					error);
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
				LOG_ERR("%s(%d) %s: %d", "CheckHwMemtestResult", gddr_inst,
					"failed", error);
			} else {
				LOG_DBG("%s(%d) %s: %d", "CheckHwMemtestResult", gddr_inst,
					"succeeded", error);
			}
		}
	}
	return any_error;
}

static int gddr_training(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPE);

	/* Check GDDR training status. */
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	bool init_errors = false;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(MRISC_INIT_TIMEOUT));

	for (uint8_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(GetDramMask(), gddr_inst)) {
			int error = CheckGddrTraining(gddr_inst, timeout);

			if (error == -ETIMEDOUT) {
				LOG_ERR("GDDR instance %d timed out during training", gddr_inst);
				init_errors = true;
			} else if (error) {
				LOG_ERR("GDDR instance %d failed training", gddr_inst);
				init_errors = true;
			}
		}
	}

	if (!init_errors) {
		if (CheckGddrHwTest() < 0) {
			LOG_ERR("GDDR HW test failed");
			return -EIO;
		}
	}

	return 0;
}
SYS_INIT(gddr_training, APPLICATION, 20);
