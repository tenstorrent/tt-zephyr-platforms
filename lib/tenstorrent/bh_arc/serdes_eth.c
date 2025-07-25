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

static inline void SetupSerdesTlb(uint32_t serdes_inst, uint32_t ring, uint64_t addr)
{
	/* Logical X,Y coordinates */
	uint8_t x, y;

	GetSerdesNocCoords(serdes_inst, ring, &x, &y);

	NOC2AXITlbSetup(ring, SERDES_ETH_SETUP_TLB, x, y, addr);
}

void LoadSerdesEthRegs(uint32_t serdes_inst, uint32_t ring, const uint8_t *tag)
{
	tt_boot_fs_fd fd_data;
	uint8_t *buf = NULL;

	if (tt_boot_fs_find_fd_by_tag(&boot_fs_data, tag, &fd_data) != TT_BOOT_FS_OK) {
		LOG_ERR("%s (%s) failed: %d", "tt_boot_fs_find_fd_by_tag", tag, -EIO);
		return;
	}
	uint32_t spi_address = fd_data.spi_addr;
	uint32_t image_size = fd_data.flags.f.image_size;

	SetupSerdesTlb(serdes_inst, 0, SERDES_INST_BASE_ADDR(serdes_inst) + CMN_OFFSET);

	for (size_t offset = 0; offset < image_size; offset += TEMP_SPI_BUFFER_SIZE) {
		size_t len = MIN(TEMP_SPI_BUFFER_SIZE, image_size - offset);

		if (spi_flash_read_data_to_buf(spi_address + offset, len, &buf)) {
			LOG_ERR("%s failed", "spi_flash_read_data_to_buf");
			return;
		}

		SerdesRegData *reg_table = (SerdesRegData *)buf;
		uint32_t reg_count = len / sizeof(SerdesRegData);

		for (uint32_t i = 0; i < reg_count; i++) {
			NOC2AXIWrite32(ring, SERDES_ETH_SETUP_TLB, reg_table[i].addr,
				       reg_table[i].data);
		}

		k_free(buf);
		buf = NULL;
	}
}

int LoadSerdesEthFw(uint32_t serdes_inst, uint32_t ring, const uint8_t *tag)
{
	tt_boot_fs_fd fd_data;

	if (tt_boot_fs_find_fd_by_tag(&boot_fs_data, tag, &fd_data) != TT_BOOT_FS_OK) {
		LOG_ERR("%s (%s) failed: %d", "tt_boot_fs_find_fd_by_tag", tag, -EIO);
		return -EIO;
	}
	uint32_t spi_address = fd_data.spi_addr;
	uint32_t image_size = fd_data.flags.f.image_size;

	SetupSerdesTlb(serdes_inst, 0, SERDES_INST_SRAM_ADDR(serdes_inst));
	volatile uint32_t *serdes_tlb =
		GetTlbWindowAddr(ring, SERDES_ETH_SETUP_TLB, SERDES_INST_SRAM_ADDR(serdes_inst));

	return spi_arc_dma_transfer_to_tile(spi_address, image_size, (uint8_t *)serdes_tlb);
}
