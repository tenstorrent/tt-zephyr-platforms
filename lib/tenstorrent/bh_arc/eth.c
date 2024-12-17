/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "serdes_eth.h"
#include "noc2axi.h"
#include "noc.h"
#include "reg.h"
#include "read_only_table.h"
#include "fw_table.h"
#include "efuse.h"

#define ETH_SETUP_TLB  0
#define ETH_PARAM_ADDR 0x7c000

#define ETH_RESET_PC_0              0xFFB14000
#define ETH_END_PC_0                0xFFB14004
#define ETH_RESET_PC_1              0xFFB14008
#define ETH_END_PC_1                0xFFB1400C
#define ETH_RISC_DEBUG_SOFT_RESET_0 0xFFB121B0

#define ETH_MAC_ADDR_ORG 0x208C47 /* 20:8C:47 */

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
	if (get_fw_table()->pci0_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable ETH 0-3 */
		eth_sel |= BIT(0) | BIT(1) | BIT(2) | BIT(3);
	} else if (get_fw_table()->pci0_property_table.num_serdes == 1) { /* Only enable ETH 2,3 */
		eth_sel |= BIT(2) | BIT(3);
	}
	if (get_fw_table()->pci1_property_table.pcie_mode ==
	    FwTable_PciPropertyTable_PcieMode_DISABLED) { /* Enable ETH 10-13 */
		eth_sel |= BIT(10) | BIT(11) | BIT(12) | BIT(13);
	} else if (get_fw_table()->pci1_property_table.num_serdes ==
		   1) { /* Only enable ETH 10,11 */
		eth_sel |= BIT(10) | BIT(11);
	}

	eth_sel &= eth_enabled;

	/* If eth_disable_mask_en is set then make sure the disabled eths are not enabled */
	if (get_fw_table()->eth_property_table.eth_disable_mask_en) {
		eth_sel &= ~get_fw_table()->eth_property_table.eth_disable_mask;
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
	uint32_t asic_id = EfuseRead(EfuseDirect, EfuseBoxFunc, FUSE_ASIC_ID_ADDR) & 0xFFFF;

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

int LoadEthFw(uint32_t eth_inst, uint32_t ring, uint8_t *fw_image, uint32_t fw_size)
{
	/* The shifting is to align the address to the lowest 16 bytes */
	/* uint32_t fw_load_addr = ((ETH_PARAM_ADDR - fw_size) >> 2) << 2; */
	uint32_t fw_load_addr = 0x00072000;

	SetupEthTlb(eth_inst, ring, fw_load_addr);
	volatile uint32_t *eth_tlb = GetTlbWindowAddr(ring, ETH_SETUP_TLB, fw_load_addr);

	bool dma_pass = ArcDmaTransfer(fw_image, (void *)eth_tlb, fw_size);

	if (!dma_pass) {
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
int LoadEthFwCfg(uint32_t eth_inst, uint32_t ring, uint32_t eth_enabled,
	uint8_t *fw_cfg_image, uint32_t fw_cfg_size)
{
	uint32_t *fw_cfg_32b = (uint32_t *)fw_cfg_image;

	/* Pass in eth_sel based on harvesting info and PCIe configuration */
	fw_cfg_32b[0] = GetEthSel(eth_enabled);

	/* Pass in some board/chip specific data for ETH to use */
	/* InitHW -> InitEth -> LoadEthFwCfg comes before init_telemtry, so cannot simply call for
	 * telemetry data here
	 */
	fw_cfg_32b[32] = get_pcb_type();
	fw_cfg_32b[33] = get_asic_location();
	fw_cfg_32b[34] = get_read_only_table()->board_id >> 32;
	fw_cfg_32b[35] = get_read_only_table()->board_id & 0xFFFFFFFF;
	/* Split the 48-bit MAC address into 2 24-bit values, separated by organisation ID and
	 * device ID
	 */
	uint64_t mac_addr_base = GetMacAddressBase();

	fw_cfg_32b[36] = (mac_addr_base >> 24) & 0xFFFFFF;
	fw_cfg_32b[37] = mac_addr_base & 0xFFFFFF;

	/* Write the ETH Param table */
	SetupEthTlb(eth_inst, ring, ETH_PARAM_ADDR);
	volatile uint32_t *eth_tlb = GetTlbWindowAddr(ring, ETH_SETUP_TLB, ETH_PARAM_ADDR);

	bool dma_pass = ArcDmaTransfer(fw_cfg_image, (void *)eth_tlb, fw_cfg_size);

	if (!dma_pass) {
		return -1;
	}

	return 0;
}
