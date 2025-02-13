/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "arc_dma.h"
#include "cm2bm_msg.h"
#include "init_common.h"
#include "irqnum.h"
#include "reg.h"
#include "spi_controller.h"
#include "spi_eeprom.h"
#include "status_reg.h"

#include <stdint.h>

#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/kernel.h>

int SpiReadWrap(uint32_t addr, uint32_t size, uint8_t *dst)
{
	SpiBlockRead(addr, size, dst);
	return TT_BOOT_FS_OK;
}

void InitSpiFS(void)
{
	/* Toggle SPI reset to clear state left by bootcode */
	SpiControllerReset();

	EepromSetup();
	tt_boot_fs_mount(&boot_fs_data, SpiReadWrap, NULL, NULL);
	SpiBufferSetup();
}

void InitResetInterrupt(uint8_t pcie_inst)
{
#if CONFIG_ARC
	if (pcie_inst == 0) {
		IRQ_CONNECT(IRQNUM_PCIE0_ERR_INTR, 0, ChipResetRequest, IRQNUM_PCIE0_ERR_INTR, 0);
		irq_enable(IRQNUM_PCIE0_ERR_INTR);
	} else if (pcie_inst == 1) {
		IRQ_CONNECT(IRQNUM_PCIE1_ERR_INTR, 0, ChipResetRequest, IRQNUM_PCIE1_ERR_INTR, 0);
		irq_enable(IRQNUM_PCIE1_ERR_INTR);
	}
#else
	ARG_UNUSED(pcie_inst);
#endif
}

void DeassertTileResets(void)
{
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
}

int InitFW(uint32_t app_version)
{
	WriteReg(STATUS_FW_VERSION_REG_ADDR, app_version);

	/* Initialize ARC DMA */
	ArcDmaConfig();
	ArcDmaInitCh(0, 0, 15);

	/* Initialize SPI EEPROM and the filesystem */
	InitSpiFS();

	return 0;
}
