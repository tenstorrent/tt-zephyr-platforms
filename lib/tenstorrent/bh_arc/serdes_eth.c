/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "serdes_eth.h"
#include "noc2axi.h"
#include "noc.h"

#include <tenstorrent/spi_flash_buf.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(eth_serdes, CONFIG_TT_APP_LOG_LEVEL);

#define SERDES_ETH_SETUP_TLB 0

static const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));

static inline void SetupSerdesTlb(uint32_t serdes_inst, uint32_t ring, uint64_t addr)
{
	/* Logical X,Y coordinates */
	uint8_t x, y;

	GetSerdesNocCoords(serdes_inst, ring, &x, &y);

	NOC2AXITlbSetup(ring, SERDES_ETH_SETUP_TLB, x, y, addr);
}

static int NOC2AxiWrite32SerdesReg(uint8_t *src, uint8_t *dst, size_t len)
{
	SerdesRegData *reg_table = (SerdesRegData *)src;
	uint32_t reg_count = len / sizeof(SerdesRegData);

	for (uint32_t i = 0; i < reg_count; i++) {
		NOC2AXIWrite32(0, SERDES_ETH_SETUP_TLB, reg_table[i].addr, reg_table[i].data);
	}
	return 0;
}

void LoadSerdesEthRegs(uint32_t serdes_inst, uint32_t ring, uint8_t *buf, size_t buf_size,
		       size_t spi_address, size_t image_size)
{
	SetupSerdesTlb(serdes_inst, ring, SERDES_INST_BASE_ADDR(serdes_inst) + CMN_OFFSET);
	spi_transfer_by_parts(flash, spi_address, image_size, buf, buf_size, NULL,
			      NOC2AxiWrite32SerdesReg);
}

int LoadSerdesEthFw(uint32_t serdes_inst, uint32_t ring, uint8_t *buf, size_t buf_size,
		    size_t spi_address, size_t image_size)
{
	int rc;

	SetupSerdesTlb(serdes_inst, 0, SERDES_INST_SRAM_ADDR(serdes_inst));
	volatile uint32_t *serdes_tlb =
		GetTlbWindowAddr(ring, SERDES_ETH_SETUP_TLB, SERDES_INST_SRAM_ADDR(serdes_inst));
	rc = spi_arc_dma_transfer_to_tile(flash, spi_address, image_size, buf, buf_size,
					  (uint8_t *)serdes_tlb);

	return rc;
}
