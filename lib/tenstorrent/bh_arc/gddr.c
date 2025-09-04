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
#include "noc_dma.h"
#include "noc2axi.h"
#include "reg.h"

#include <tenstorrent/post_code.h>
#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/sys_init_defines.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/drivers/memc/memc_tt_bh.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>

static const struct device *memc_devices[] = {DT_INST_FOREACH_STATUS_OKAY(MEMC_TT_BH_DEVICE_GET)};

/* This is the noc2axi instance we want to run the MRISC FW on */
#define MRISC_FW_NOC2AXI_PORT 0
#define MRISC_SETUP_TLB       13
#define MRISC_L1_ADDR         (1ULL << 37)
#define MRISC_REG_ADDR        (1ULL << 40)
#define MRISC_FW_CFG_OFFSET   0x3C00
#define ARC_NOC0_X            8
#define ARC_NOC0_Y            0
#define MRISC_L1_SIZE         (128 * 1024)

#define MRISC_FW_TAG     "memfw"
#define MRISC_FW_CFG_TAG "memfwcfg"

LOG_MODULE_REGISTER(gddr, CONFIG_TT_APP_LOG_LEVEL);

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

static uint32_t MriscL1Read32(uint8_t gddr_inst, uint32_t addr)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	return NOC2AXIRead32(0, MRISC_SETUP_TLB, MRISC_L1_ADDR + addr);
}

static void MriscL1Write32(uint8_t gddr_inst, uint32_t addr, uint32_t val)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	NOC2AXIWrite32(0, MRISC_SETUP_TLB, MRISC_L1_ADDR + addr, val);
}

static uint32_t MriscRegRead32(uint8_t gddr_inst, uint32_t addr)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_REG_ADDR + addr);
	return NOC2AXIRead32(0, MRISC_SETUP_TLB, MRISC_REG_ADDR + addr);
}

static void MriscRegWrite32(uint8_t gddr_inst, uint32_t addr, uint32_t val)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_REG_ADDR + addr);
	NOC2AXIWrite32(0, MRISC_SETUP_TLB, MRISC_REG_ADDR + addr, val);
}

static uint32_t GetDramMask(void)
{
	uint32_t dram_mask = tile_enable.gddr_enabled; /* bit mask */

	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->has_dram_table &&
	    tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask_en) {
		dram_mask &= tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask;
	}
	return dram_mask;
}

static int StartHwMemtest(const struct device *dev, uint32_t addr_bits, uint32_t start_addr,
			  uint32_t mask)
{
	uint8_t gddr_inst = memc_tt_bh_inst_get(dev);
	uint32_t msg_args[3] = {addr_bits, start_addr, mask};

	/* Only run if MRISC FW support it. Must be > 2.6 */
	gddr_telemetry_table_t gddr_telemetry;

	if (memc_tt_bh_telemetry_get(dev, &gddr_telemetry) < 0) {
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

static int CheckHwMemtestResult(const struct device *dev, k_timepoint_t timeout)
{
	uint8_t gddr_inst = memc_tt_bh_inst_get(dev);

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

	ARRAY_FOR_EACH_PTR(memc_devices, devp) {
		const struct device *dev = *devp;
		uint8_t gddr_inst = memc_tt_bh_inst_get(dev);

		if (IS_BIT_SET(tile_enable.gddr_enabled, gddr_inst)) {
			int error = StartHwMemtest(dev, 26, 0, 0);

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

	ARRAY_FOR_EACH_PTR(memc_devices, devp) {
		const struct device *dev = *devp;
		uint8_t gddr_inst = memc_tt_bh_inst_get(dev);

		if (IS_BIT_SET(test_started, gddr_inst)) {
			int error = CheckHwMemtestResult(dev, timeout);

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
		/* this is needed to securely wipe DRAM */
		if (CheckGddrHwTest() < 0) {
			LOG_ERR("GDDR HW test failed");
			return -EIO;
		}
	}

	return 0;
}
SYS_INIT_APP(gddr_training);
