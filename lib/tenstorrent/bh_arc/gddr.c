/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <reg.h>
#include <noc.h>
#include <noc2axi.h>
#include <arc_dma.h>

#include "gddr.h"
#include "harvesting.h"

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/misc/bh_fwtable.h>

/* This is the noc2axi instance we want to run the MRISC FW on */
#define MRISC_FW_NOC2AXI_PORT 0
#define MRISC_SETUP_TLB       13
#define MRISC_L1_ADDR         (1ULL << 37)
#define MRISC_REG_ADDR        (1ULL << 40)
#define MRISC_FW_CFG_OFFSET   0x3C00

LOG_MODULE_REGISTER(gddr, CONFIG_TT_APP_LOG_LEVEL);

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
