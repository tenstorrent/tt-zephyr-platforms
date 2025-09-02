/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_memc

#include "arc_dma.h"
#include "harvesting.h"
#include "init.h"
#include "noc.h"
#include "noc_dma.h"
#include "noc2axi.h"
#include "reg.h"
#include "gddr_telemetry_table.h"

#include <stdbool.h>

#include <tenstorrent/post_code.h>
#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/memc/memc_tt_bh.h>
#include <zephyr/drivers/misc/bh_fwtable.h>

#define ARC_NOC0_X    8
#define ARC_NOC0_Y    0
#define MRISC_L1_SIZE (128 * 1024)

#define MIN_GDDR_SPEED             12000
#define MAX_GDDR_SPEED             20000
#define GDDR_SPEED_TO_MEMCLK_RATIO 16
#define NUM_GDDR                   8
#define NUM_MRISC_NOC2AXI_PORT     3

/* MRISC FW telemetry base addr */
#define GDDR_TELEMETRY_TABLE_ADDR 0x8000
#define GDDR_MSG_STRUCT_ADDR      0x6000

#define RISC_CTRL_A_SCRATCH_0__REG_ADDR 0xFFB14010
#define RISC_CTRL_A_SCRATCH_1__REG_ADDR 0xFFB14014
#define RISC_CTRL_A_SCRATCH_2__REG_ADDR 0xFFB14018
#define MRISC_INIT_STATUS               RISC_CTRL_A_SCRATCH_0__REG_ADDR
#define MRISC_POST_CODE                 RISC_CTRL_A_SCRATCH_1__REG_ADDR
#define MRISC_MSG_REGISTER              RISC_CTRL_A_SCRATCH_2__REG_ADDR

#define MRISC_INIT_FINISHED   0xdeadbeef
#define MRISC_INIT_FAILED     0xfa11
#define MRISC_INIT_BEFORE     0x11111111
#define MRISC_INIT_STARTED    0x0
#define MRISC_INIT_TIMEOUT    1000 /* In ms */
#define MRISC_MEMTEST_TIMEOUT 1000 /* In ms */

/* Defined by MRISC FW */
#define MRISC_MSG_TYPE_NONE        0
#define MRISC_MSG_TYPE_RUN_MEMTEST 8

/* This is the noc2axi instance we want to run the MRISC FW on */
#define MRISC_FW_NOC2AXI_PORT 0
#define MRISC_SETUP_TLB       13
#define MRISC_L1_ADDR         (1ULL << 37)
#define MRISC_REG_ADDR        (1ULL << 40)
#define MRISC_FW_CFG_OFFSET   0x3C00

#define MRISC_FW_TAG     "memfw"
#define MRISC_FW_CFG_TAG "memfwcfg"

struct memc_tt_bh_data {
};

LOG_MODULE_REGISTER(memc_tt_bh, CONFIG_MEMC_LOG_LEVEL);

static uint32_t GetGddrSpeedFromCfg(uint8_t *fw_cfg_image)
{
	/* GDDR speed is the second DWORD of the MRISC FW Config table */
	uint32_t *fw_cfg_dw = (uint32_t *)fw_cfg_image;
	return fw_cfg_dw[1];
}

static volatile void *SetupMriscL1Tlb(const struct device *dev)
{
	const struct memc_tt_bh_config *config = dev->config;
	uint8_t gddr_inst = config->inst;
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	return GetTlbWindowAddr(0, MRISC_SETUP_TLB, MRISC_L1_ADDR);
}

static uint32_t MriscL1Read32(const struct device *dev, uint32_t addr)
{
	const struct memc_tt_bh_config *config = dev->config;
	uint8_t gddr_inst = config->inst;
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_L1_ADDR);
	return NOC2AXIRead32(0, MRISC_SETUP_TLB, MRISC_L1_ADDR + addr);
}

static void MriscRegWrite32(uint8_t gddr_inst, uint32_t addr, uint32_t val)
{
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, MRISC_REG_ADDR + addr);
	NOC2AXIWrite32(0, MRISC_SETUP_TLB, MRISC_REG_ADDR + addr, val);
}

static void ReleaseMriscReset(uint8_t gddr_inst)
{
	const uint32_t kSoftReset0Addr = 0xFFB121B0;
	uint8_t x, y;

	GetGddrNocCoords(gddr_inst, MRISC_FW_NOC2AXI_PORT, 0, &x, &y);
	NOC2AXITlbSetup(0, MRISC_SETUP_TLB, x, y, kSoftReset0Addr);

	volatile uint32_t *soft_reset_0 = GetTlbWindowAddr(0, MRISC_SETUP_TLB, kSoftReset0Addr);
	*soft_reset_0 &= ~(1 << 11); /* Clear bit corresponding to MRISC reset */
}

static void SetAxiEnable(uint8_t gddr_inst, uint8_t noc2axi_port, bool axi_enable)
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

static uint32_t GetDramMask(const struct device *const fwtable_dev)
{
	uint32_t dram_mask = tile_enable.gddr_enabled; /* bit mask */

	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->has_dram_table &&
	    tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask_en) {
		dram_mask &= tt_bh_fwtable_get_fw_table(fwtable_dev)->dram_table.dram_mask;
	}
	return dram_mask;
}

static int _memc_tt_bh_telemetry_get(const struct device *dev,
				     gddr_telemetry_table_t *gddr_telemetry)
{
	volatile uint8_t *mrisc_l1 = SetupMriscL1Tlb(dev);
	bool dma_pass = ArcDmaTransfer((const void *)(mrisc_l1 + GDDR_TELEMETRY_TABLE_ADDR),
				       gddr_telemetry, sizeof(*gddr_telemetry));
	if (!dma_pass) {
		/* If DMA failed, can read 32b at a time via NOC2AXI */
		for (int i = 0; i < sizeof(*gddr_telemetry) / 4; i++) {
			((uint32_t *)gddr_telemetry)[i] =
				MriscL1Read32(dev, GDDR_TELEMETRY_TABLE_ADDR + i * 4);
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

static const struct memc_tt_bh_api _memc_tt_bh_api = {
	.telemetry_get = _memc_tt_bh_telemetry_get,
};

/* This function assumes that tensix L1s have already been cleared */
static void wipe_l1(const struct device *dev)
{
	uint8_t noc_id = 0;
	uint64_t addr = 0;
	uint32_t dram_mask = GetDramMask(dev);
	uint8_t tensix_x = 1;
	uint8_t tensix_y = 2;

	for (uint32_t gddr_inst = 0; gddr_inst < NUM_GDDR; gddr_inst++) {
		if (IS_BIT_SET(dram_mask, gddr_inst)) {
			for (uint32_t noc2axi_port = 0; noc2axi_port < NUM_MRISC_NOC2AXI_PORT;
			     noc2axi_port++) {
				uint8_t x, y;

				GetGddrNocCoords(gddr_inst, noc2axi_port, noc_id, &x, &y);
				/* AXI enable must not be set, using MRISC address 0 */
				noc_dma_write(tensix_x, tensix_y, addr, x, y, addr, MRISC_L1_SIZE,
					      true);
			}
		}
	}
}

static int memc_tt_bh_init(const struct device *dev)
{
	const struct memc_tt_bh_config *config = dev->config;
	uint8_t gddr_inst = config->inst;
	static bool loaded_common;

	if (!loaded_common) {
		wipe_l1(dev);

		/* Load MRISC (DRAM RISC) FW to all DRAMs in the middle NOC node */
		for (uint8_t noc2axi_port = 0; noc2axi_port < 3; noc2axi_port++) {
			SetAxiEnable(gddr_inst, noc2axi_port, true);
		}

		/* Only perform this operation once */
		loaded_common = true;
	}

	uint32_t dram_mask = GetDramMask(dev);

	int rc;
	tt_boot_fs_fd tag_fd;
	size_t image_size;
	size_t spi_address;

	uint8_t buf[CONFIG_MEMC_TT_BH_BUF_SIZE] __aligned(4);

	if (!IS_BIT_SET(dram_mask, gddr_inst)) {
		LOG_DBG("memc%d is not enabled. Skipping init.", config->inst);
		return 0;
	}

	/* FIXME: used fixed partitions */
	rc = tt_boot_fs_find_fd_by_tag(config->flash_dev, MRISC_FW_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s (%s) failed: %d", "tt_boot_fs_find_fd_by_tag", MRISC_FW_TAG, rc);
		return rc;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	LOG_DBG("Loading memc%d firmware..", gddr_inst);
	if (spi_arc_dma_transfer_to_tile(config->flash_dev, spi_address, image_size, buf,
					 sizeof(buf), (uint8_t *)SetupMriscL1Tlb(dev))) {
		LOG_ERR("%s(%d) failed: %d", "spi_arc_dma_transfer_to_tile", gddr_inst, -EIO);
		return -EIO;
	}

	rc = tt_boot_fs_find_fd_by_tag(config->flash_dev, MRISC_FW_CFG_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s (%s) failed: %d", "tt_boot_fs_find_fd_by_tag", MRISC_FW_CFG_TAG, rc);
		return rc;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	/* Loading ETH FW configuration data requires the whole data to be loaded into buffer */
	__ASSERT(sizeof(buf) >= image_size,
		 "spi buffer size %zu must be larger than image size %zu", sizeof(buf), image_size);

	rc = flash_read(config->flash_dev, spi_address, buf, image_size);
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "flash_read", rc);
		return rc;
	}

	uint32_t gddr_speed = GetGddrSpeedFromCfg(buf);

	if (!IN_RANGE(gddr_speed, MIN_GDDR_SPEED, MAX_GDDR_SPEED)) {
		LOG_WRN("%s() failed: %d", "GetGddrSpeedFromCfg", gddr_speed);
		gddr_speed = MIN_GDDR_SPEED;
	}

	if (clock_control_set_rate(config->pll_dev, (clock_control_subsys_t)config->clock_channel,
				   (clock_control_subsys_rate_t)(gddr_speed / config->clock_div))) {
		LOG_ERR("%s(%d) failed: %d", "clock_control_set_rate", gddr_speed, -EIO);
		return -EIO;
	}

	LOG_DBG("Loading memc%d firmware config..", gddr_inst);
	if (spi_arc_dma_transfer_to_tile(config->flash_dev, spi_address, image_size, buf,
					 sizeof(buf),
					 (uint8_t *)SetupMriscL1Tlb(dev) + MRISC_FW_CFG_OFFSET)) {
		LOG_ERR("%s(%d) failed: %d", "LoadMriscFwCfg", gddr_inst, -EIO);
		return -EIO;
	}
	MriscRegWrite32(gddr_inst, MRISC_INIT_STATUS, MRISC_INIT_BEFORE);
	ReleaseMriscReset(gddr_inst);

	LOG_DBG("memc%d initialized successfully", gddr_inst);

	return 0;
}

#define DEFINE_MEMC_TT_BH(_inst)                                                                   \
	static const struct memc_tt_bh_config memc_tt_bh_config_##_inst = {                        \
		.pll_dev = DEVICE_DT_GET(DT_INST_PHANDLE_BY_IDX(_inst, clocks, 0)),                \
		.flash_dev = DEVICE_DT_GET(DT_INST_PHANDLE(_inst, flash)),                         \
		.fwtable_dev = DEVICE_DT_GET(DT_INST_PHANDLE(_inst, fwtable)),                     \
		.inst = DT_INST_REG_ADDR(_inst),                                                   \
		.clock_channel = DT_INST_PROP_BY_IDX(_inst, clock_channels, 0),                    \
		.clock_div = DT_INST_PROP_BY_IDX(_inst, clock_divs, 0),                            \
	};                                                                                         \
	static struct memc_tt_bh_data memc_tt_bh_data_##_inst;                                     \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, memc_tt_bh_init, PM_DEVICE_DT_INST_GET(_inst),                \
			      &memc_tt_bh_data_##_inst, &memc_tt_bh_config_##_inst, POST_KERNEL,   \
			      CONFIG_MEMC_TT_BH_INIT_PRIORITY, &_memc_tt_bh_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_MEMC_TT_BH)
