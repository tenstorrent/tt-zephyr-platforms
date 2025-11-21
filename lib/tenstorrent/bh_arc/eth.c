/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "functional_efuse.h"
#include "eth.h"
#include "harvesting.h"
#include "init.h"
#include "noc.h"
#include "noc_init.h"
#include "noc2axi.h"
#include "reg.h"
#include "serdes_eth.h"

#include <tenstorrent/post_code.h>
#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/sys_init_defines.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/init.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_tt_bh_noc.h>
#include <zephyr/drivers/dma/dma_arc_hs.h>

LOG_MODULE_REGISTER(eth, CONFIG_TT_APP_LOG_LEVEL);

#define ETH_SETUP_TLB  0
#define ETH_PARAM_ADDR 0x7c000

#define ERISC_L1_SIZE (512 * 1024)

#define ETH_RESET_PC_0              0xFFB14000
#define ETH_END_PC_0                0xFFB14004
#define ETH_RESET_PC_1              0xFFB14008
#define ETH_END_PC_1                0xFFB1400C
#define ETH_RISC_DEBUG_SOFT_RESET_0 0xFFB121B0

#define ETH_MAC_ADDR_ORG 0x208C47 /* 20:8C:47 */

#define ETH_FW_CFG_TAG "ethfwcfg"
#define ETH_FW_TAG     "ethfw"
#define ETH_SD_REG_TAG "ethsdreg"
#define ETH_SD_FW_TAG  "ethsdfw"

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));
static const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));
static const struct device *dma_noc = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dma1));
static const struct device *const arc_dma_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(dma0));

typedef struct {
	uint32_t sd_mode_sel_0: 1;
	uint32_t sd_mode_sel_1: 1;
	uint32_t reserved_2: 1;
	uint32_t mux_sel: 2;
	uint32_t master_sel_0: 2;
	uint32_t master_sel_1: 2;
	uint32_t master_sel_2: 2;
	uint32_t reserved_31_11: 21;
} RESET_UNIT_PCIE_MISC_CNTL3_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_PCIE_MISC_CNTL3_reg_t f;
} RESET_UNIT_PCIE_MISC_CNTL3_reg_u;

#define RESET_UNIT_PCIE_MISC_CNTL3_REG_DEFAULT 0x00000000

#define RESET_UNIT_PCIE1_MISC_CNTL_3_REG_ADDR 0x8003050C
#define RESET_UNIT_PCIE_MISC_CNTL_3_REG_ADDR  0x8003009C

static inline void SetupEthTlb(uint32_t eth_inst, uint32_t ring, uint64_t addr)
{
	/* Logical X,Y coordinates */
	uint8_t x, y;

	GetEthNocCoords(eth_inst, ring, &x, &y);

	NOC2AXITlbSetup(ring, ETH_SETUP_TLB, x, y, addr);
}

void SetupEthSerdesMux(uint32_t eth_enabled)
{
	RESET_UNIT_PCIE_MISC_CNTL3_reg_u pcie_misc_cntl3_reg, pcie1_misc_cntl3_reg;

	pcie_misc_cntl3_reg.val = ReadReg(RESET_UNIT_PCIE_MISC_CNTL_3_REG_ADDR);
	pcie1_misc_cntl3_reg.val = ReadReg(RESET_UNIT_PCIE1_MISC_CNTL_3_REG_ADDR);

	/* 4,5,6 */
	if (!IS_BIT_SET(eth_enabled, 4)) {
		pcie_misc_cntl3_reg.f.mux_sel = 0b11;
	} else if (!IS_BIT_SET(eth_enabled, 5)) {
		pcie_misc_cntl3_reg.f.mux_sel = 0b10;
	} else if (!IS_BIT_SET(eth_enabled, 6)) {
		pcie_misc_cntl3_reg.f.mux_sel = 0b00;
	}

	/* 7,8,9 */
	if (!IS_BIT_SET(eth_enabled, 7)) {
		pcie1_misc_cntl3_reg.f.mux_sel = 0b00;
	} else if (!IS_BIT_SET(eth_enabled, 8)) {
		pcie1_misc_cntl3_reg.f.mux_sel = 0b10;
	} else if (!IS_BIT_SET(eth_enabled, 9)) {
		pcie1_misc_cntl3_reg.f.mux_sel = 0b11;
	}

	WriteReg(RESET_UNIT_PCIE_MISC_CNTL_3_REG_ADDR, pcie_misc_cntl3_reg.val);
	WriteReg(RESET_UNIT_PCIE1_MISC_CNTL_3_REG_ADDR, pcie1_misc_cntl3_reg.val);
}

uint32_t GetEthSel(uint32_t eth_enabled)
{
	uint32_t eth_sel = 0;

	/* Turn on the correct ETH instances based on the mux selects */
	/* Mux selects should be set earlier in the init sequence, when reading */
	/* efuses and setting up harvesting information */
	RESET_UNIT_PCIE_MISC_CNTL3_reg_u pcie_misc_cntl3_reg, pcie1_misc_cntl3_reg;

	pcie_misc_cntl3_reg.val = ReadReg(RESET_UNIT_PCIE_MISC_CNTL_3_REG_ADDR);
	pcie1_misc_cntl3_reg.val = ReadReg(RESET_UNIT_PCIE1_MISC_CNTL_3_REG_ADDR);

	if (pcie_misc_cntl3_reg.f.mux_sel == 0b00) {
		eth_sel |= BIT(4) | BIT(5); /* ETH 4, 5 */
		/* 0b00 is invalid/not used */
	} else if (pcie_misc_cntl3_reg.f.mux_sel == 0b10) {
		eth_sel |= BIT(4) | BIT(6); /* ETH 4, 6 */
	} else if (pcie_misc_cntl3_reg.f.mux_sel == 0b11) {
		eth_sel |= BIT(5) | BIT(6); /* ETH 5, 6 */
	}

	if (pcie1_misc_cntl3_reg.f.mux_sel == 0b00) {
		eth_sel |= BIT(9) | BIT(8); /* ETH 9, 8 */
		/* 0b00 is invalid/not used */
	} else if (pcie1_misc_cntl3_reg.f.mux_sel == 0b10) {
		eth_sel |= BIT(9) | BIT(7); /* ETH 9, 7 */
	} else if (pcie1_misc_cntl3_reg.f.mux_sel == 0b11) {
		eth_sel |= BIT(8) | BIT(7); /* ETH 8, 7 */
	}

	/* Turn on the correct ETH instances based on pcie serdes properties */
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci0_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) {
		/* Enable ETH 0-3 */
		eth_sel |= BIT(0) | BIT(1) | BIT(2) | BIT(3);
	} else if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci0_property_table.num_serdes == 1) {
		/* Only enable ETH 2,3 */
		eth_sel |= BIT(2) | BIT(3);
	}
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci1_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) {
		/* Enable ETH 10-13 */
		eth_sel |= BIT(10) | BIT(11) | BIT(12) | BIT(13);
	} else if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci1_property_table.num_serdes == 1) {
		/* Only enable ETH 10,11 */
		eth_sel |= BIT(10) | BIT(11);
	}

	eth_sel &= eth_enabled;

	/* If eth_disable_mask_en is set then make sure the disabled eths are not enabled */
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->eth_property_table.eth_disable_mask_en) {
		eth_sel &= ~tt_bh_fwtable_get_fw_table(fwtable_dev)
				    ->eth_property_table.eth_disable_mask;
	}

	/* Make sure to send the mux_sel information as well so the ETH can configure itself
	 * correctly to SerDes lanes
	 * This is mainly for edge cases where a mux_sel enabled ETH is forcefilly disabled by the
	 * eth_disable_mask
	 * e.g. if pcie0 mux_sel is 0b00, ETH4 goes to SerDes 3 Lane 3:0, ETH5 goes to SerDes 3 Lane
	 * 7:4 but eth_disable_mask is 0b10000, then ETH4 is disabled and only ETH5 is enabled via
	 * eth_sel, at which point it becomes ambiguous which SerDes lane ETH5 should be connected
	 * to (3:0 or 7:4?)
	 * having the mux_sel information will allow ETH5 to disambiguate this
	 */
	return (pcie1_misc_cntl3_reg.f.mux_sel << 24) | (pcie_misc_cntl3_reg.f.mux_sel << 16) |
	       eth_sel;
}

uint64_t GetMacAddressBase(void)
{
	uint32_t asic_id = READ_FUNCTIONAL_EFUSE(ASIC_ID_LOW) & 0xFFFF;

	/* TODO: This will later be updated with the final code to create unique base MAC addresses
	 */
	uint32_t mac_addr_base_id = asic_id * 12;

	/* Base MAC address is 48 bits, concatenation of 2 24-bit values */
	uint64_t mac_addr_base = ((uint64_t)ETH_MAC_ADDR_ORG << 24) | (uint64_t)mac_addr_base_id;

	return mac_addr_base;
}

void ReleaseEthReset(uint32_t eth_inst, uint32_t ring)
{
	SetupEthTlb(eth_inst, ring, ETH_RESET_PC_0);

	volatile uint32_t *soft_reset_0 =
		GetTlbWindowAddr(ring, ETH_SETUP_TLB, ETH_RISC_DEBUG_SOFT_RESET_0);
	*soft_reset_0 &= ~(1 << 11); /* Clear bit for RISC0 reset, leave RISC1 in reset still */
}

int LoadEthFw(uint32_t eth_inst, uint32_t ring, uint8_t *buf, size_t buf_size, size_t spi_address,
	      size_t image_size)
{
	/* The shifting is to align the address to the lowest 16 bytes */
	/* uint32_t fw_load_addr = ((ETH_PARAM_ADDR - fw_size) >> 2) << 2; */
	uint32_t fw_load_addr = 0x00070000;

	SetupEthTlb(eth_inst, ring, fw_load_addr);
	volatile uint32_t *eth_tlb = GetTlbWindowAddr(ring, ETH_SETUP_TLB, fw_load_addr);

	if (spi_arc_dma_transfer_to_tile(flash, spi_address, image_size, buf, buf_size,
					 (uint8_t *)eth_tlb)) {
		return -1;
	}

	SetupEthTlb(eth_inst, ring, ETH_RESET_PC_0);
	NOC2AXIWrite32(ring, ETH_SETUP_TLB, ETH_RESET_PC_0, fw_load_addr);
	NOC2AXIWrite32(ring, ETH_SETUP_TLB, ETH_END_PC_0, ETH_PARAM_ADDR - 0x4);

	return 0;
}

/**
 * @brief Load the ETH FW configuration data into ETH L1 memory
 * @param eth_inst ETH instance to load the FW config for
 * @param ring Load over NOC 0 or NOC 1
 * @param eth_enabled Bitmask of enabled ETH instances
 * @param fw_cfg_image Pointer to the FW config data
 * @param fw_cfg_size Size of the FW config data
 * @return int 0 on success, -1 on failure
 */
int LoadEthFwCfg(uint32_t eth_inst, uint32_t ring, uint8_t *buf, uint32_t eth_enabled,
		 size_t spi_address, size_t image_size)
{
	int rc;

	rc = flash_read(flash, spi_address, buf, image_size);
	if (rc < 0) {
		LOG_ERR("%s() failed: %d", "flash_read", rc);
		return rc;
	}

	uint32_t *fw_cfg_32b = (uint32_t *)buf;

	/* Pass in eth_sel based on harvesting info and PCIe configuration */
	fw_cfg_32b[0] = GetEthSel(eth_enabled);

	/* Check if speed overrides exist, */
	/* apply them if they are a valid speed setting (40G, 100G, 200G, 400G) */
	uint32_t speed_override =
		tt_bh_fwtable_get_fw_table(fwtable_dev)->eth_property_table.eth_speed_override;

	if (speed_override == 40 || speed_override == 100 || speed_override == 200 ||
	    speed_override == 400) {
		fw_cfg_32b[1] = speed_override;
	}

	/* Pass in some board/chip specific data for ETH to use */
	/* InitHW -> InitEth -> LoadEthFwCfg comes before init_telemtry, so cannot simply call for
	 * telemetry data here
	 */
	fw_cfg_32b[32] = tt_bh_fwtable_get_pcb_type(fwtable_dev);
	fw_cfg_32b[33] = tt_bh_fwtable_get_asic_location(fwtable_dev);
	fw_cfg_32b[34] = tt_bh_fwtable_get_read_only_table(fwtable_dev)->board_id >> 32;
	fw_cfg_32b[35] = tt_bh_fwtable_get_read_only_table(fwtable_dev)->board_id & 0xFFFFFFFF;
	/* Split the 48-bit MAC address into 2 24-bit values, separated by organisation ID and
	 * device ID
	 */
	uint64_t mac_addr_base = GetMacAddressBase();

	fw_cfg_32b[36] = (mac_addr_base >> 24) & 0xFFFFFF;
	fw_cfg_32b[37] = mac_addr_base & 0xFFFFFF;

	fw_cfg_32b[38] = READ_FUNCTIONAL_EFUSE(ASIC_ID_HIGH);
	fw_cfg_32b[39] = READ_FUNCTIONAL_EFUSE(ASIC_ID_LOW);
	fw_cfg_32b[40] = tile_enable.eth_enabled;

	/* Write the ETH Param table */
	SetupEthTlb(eth_inst, ring, ETH_PARAM_ADDR);
	volatile uint32_t *eth_tlb = GetTlbWindowAddr(ring, ETH_SETUP_TLB, ETH_PARAM_ADDR);

	if (dma_arc_hs_transfer(arc_dma_dev, 0, buf, (void *)eth_tlb, image_size) < 0) {
		LOG_ERR("DMA transfer failed");
		return -1;
	}

	return 0;
}

static void SerdesEthInit(void)
{
	uint32_t ring = 0;
	int rc;
	tt_boot_fs_fd tag_fd;
	size_t image_size;
	size_t spi_address;

	SetupEthSerdesMux(tile_enable.eth_enabled);

	uint32_t load_serdes = BIT(2) | BIT(5); /* Serdes 2, 5 are always for ETH */
	/* Select the other ETH Serdes instances based on pcie serdes properties */
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci0_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable Serdes 0, 1 */
		load_serdes |= BIT(0) | BIT(1);
	} else if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci0_property_table.num_serdes ==
		   1) { /* Just enable Serdes 1 */
		load_serdes |= BIT(1);
	}
	if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci1_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable Serdes 3, 4 */
		load_serdes |= BIT(3) | BIT(4);
	} else if (tt_bh_fwtable_get_fw_table(fwtable_dev)->pci1_property_table.num_serdes ==
		   1) { /* Just enable Serdes 4 */
		load_serdes |= BIT(4);
	}

	uint8_t buf[SCRATCHPAD_SIZE] __aligned(4);

	rc = tt_boot_fs_find_fd_by_tag(flash, ETH_SD_REG_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_find_fd_by_tag", ETH_SD_REG_TAG, rc);
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	/* Load fw regs */
	for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
		if (load_serdes & (1 << serdes_inst)) {
			LoadSerdesEthRegs(serdes_inst, ring, buf, SCRATCHPAD_SIZE, spi_address,
					  image_size);
		}
	}

	rc = tt_boot_fs_find_fd_by_tag(flash, ETH_SD_FW_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_find_fd_by_tag", ETH_SD_FW_TAG, rc);
		return;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	/* Load fw */
	for (uint8_t serdes_inst = 0; serdes_inst < 6; serdes_inst++) {
		if (load_serdes & (1 << serdes_inst)) {
			LoadSerdesEthFw(serdes_inst, ring, buf, SCRATCHPAD_SIZE, spi_address,
					image_size);
		}
	}
}

/* This function assumes that tensix L1s have already been cleared */
static void wipe_l1(void)
{
	uint8_t noc_id = 0;
	uint64_t addr = 0;
	uint8_t tensix_x, tensix_y;

	GetEnabledTensix(&tensix_x, &tensix_y);

	struct tt_bh_dma_noc_coords coords =
		tt_bh_dma_noc_coords_init(tensix_x, tensix_y, 0, 0);

	struct dma_block_config block = {
		.source_address = addr,
		.dest_address = addr,
		.block_size = ERISC_L1_SIZE,
	};

	struct dma_config config = {
		.channel_direction = PERIPHERAL_TO_MEMORY,
		.source_data_size = 1,
		.dest_data_size = 1,
		.source_burst_length = 1,
		.dest_burst_length = 1,
		.block_count = 1,
		.head_block = &block,
		.user_data = &coords,
	};

	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			uint8_t x, y;

			GetEthNocCoords(eth_inst, noc_id, &x, &y);

			coords.dest_x = x;
			coords.dest_y = y;

			dma_config(dma_noc, 1, &config);
			dma_start(dma_noc, 1);
		}
	}
}

static void EthInit(void)
{
	uint32_t ring = 0;
	int rc;
	tt_boot_fs_fd tag_fd;
	size_t image_size;
	size_t spi_address;

	/* Early exit if no ETH tiles enabled */
	if (tile_enable.eth_enabled == 0) {
		return;
	}

	wipe_l1();

	uint8_t buf[SCRATCHPAD_SIZE] __aligned(4);

	rc = tt_boot_fs_find_fd_by_tag(flash, ETH_FW_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_find_fd_by_tag", ETH_FW_TAG, rc);
		return;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	/* Load fw */
	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			LoadEthFw(eth_inst, ring, buf, SCRATCHPAD_SIZE, spi_address, image_size);
		}
	}

	rc = tt_boot_fs_find_fd_by_tag(flash, ETH_FW_CFG_TAG, &tag_fd);
	if (rc < 0) {
		LOG_ERR("%s(%s) failed: %d", "tt_boot_fs_find_fd_by_tag", ETH_FW_CFG_TAG, rc);
		return;
	}
	image_size = tag_fd.flags.f.image_size;
	spi_address = tag_fd.spi_addr;

	/* Loading ETH FW configuration data requires the whole data to be loaded into buffer */
	__ASSERT(SCRATCHPAD_SIZE >= image_size,
		 "spi buffer size %zu must be larger than image size %zu", SCRATCHPAD_SIZE,
		 image_size);

	/* Load param table */
	for (uint8_t eth_inst = 0; eth_inst < MAX_ETH_INSTANCES; eth_inst++) {
		if (tile_enable.eth_enabled & BIT(eth_inst)) {
			LoadEthFwCfg(eth_inst, ring, buf, tile_enable.eth_enabled, spi_address,
				     image_size);
			ReleaseEthReset(eth_inst, ring);
		}
	}
}

static int eth_init(void)
{
	/* TODO: Load ERISC (Ethernet RISC) FW to all ethernets (8 of them) */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPA);
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	SerdesEthInit();
	EthInit();

	return 0;
}
SYS_INIT_APP(eth_init);
