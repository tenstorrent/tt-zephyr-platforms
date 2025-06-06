/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <stdint.h>
#include <zephyr/drivers/i2c.h>

#include "status_reg.h"
#include "init_common.h"
#include "pll.h"
#include "aiclk_ppm.h"

// #include "fw_table.h"
// #include "flash_info_table.h"

static inline void delay_spin(uint32_t count)
{
	volatile uint32_t i = count;

	while (i--) {
		/* Spin */
	}
}

static void DeassertRiscvResets(void)
{
	for (uint32_t i = 0; i < 8; i++) {
		sys_write32(RESET_UNIT_TENSIX_RISC_RESET_0_REG_ADDR + i * 4, 0xffffffff);
	}

	RESET_UNIT_ETH_RESET_reg_u eth_reset;

	eth_reset.val = sys_read32(RESET_UNIT_ETH_RESET_REG_ADDR);
	eth_reset.f.eth_risc_reset_n = 0x3fff;
	sys_write32(RESET_UNIT_ETH_RESET_REG_ADDR, eth_reset.val);

	RESET_UNIT_DDR_RESET_reg_u ddr_reset;

	ddr_reset.val = sys_read32(RESET_UNIT_DDR_RESET_REG_ADDR);
	ddr_reset.f.ddr_risc_reset_n = 0xffffff;
	sys_write32(RESET_UNIT_DDR_RESET_REG_ADDR, ddr_reset.val);
}

void soc_early_init_hook(void)
{
	/* Write a status register indicating HW init progress */
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};
	STATUS_ERROR_STATUS0_reg_u error_status0 = {0};

	boot_status0.val = sys_read32(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitStarted;
	sys_write32(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		// load_fw_table(large_sram_buffer, SCRATCHPAD_SIZE);
	}
	// load_read_only_table(large_sram_buffer, SCRATCHPAD_SIZE);
	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		// load_flash_info_table(large_sram_buffer, SCRATCHPAD_SIZE);
	}

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		/* Go back to PLL bypass, since RISCV resets need to be deasserted at low speed */
		PLLAllBypass();
		/* Deassert RISC reset from reset_unit */
		DeassertRiscvResets();
		/* Initialize some AICLK tracking variables */
		InitAiclkPPM();
	}

	if (IS_ENABLED(CONFIG_I2C)) {
		uint32_t reg;
		/* Manually toggle I2C reset control bits */
		volatile uint32_t *RESET_UNIT_I2C_CNTL = (uint32_t *)0x800300F0;

		reg = *RESET_UNIT_I2C_CNTL;
		/* Disable I2C controllers and set reset bit */
		*RESET_UNIT_I2C_CNTL = BIT(4);
		delay_spin(1000);
		/* Clear reset bit */
		*RESET_UNIT_I2C_CNTL = (reg & ~BIT(4));
	}
	if (DT_HAS_COMPAT_STATUS_OKAY(snps_designware_ssi) && IS_ENABLED(CONFIG_MSPI)) {
		/* Manually toggle the SPI reset control bits */
		volatile uint32_t *RESET_UNIT_SPI_CNTL = (uint32_t *)0x800300F8;
		*RESET_UNIT_SPI_CNTL |= BIT(4);
		/* Delay a few cycles- pre kernel so just use a spin loop */
		delay_spin(1000);
		*RESET_UNIT_SPI_CNTL &= ~BIT(4);
		/* Enable the SPI */
		*RESET_UNIT_SPI_CNTL |= BIT(0);
		/* Disable DDR mode */
		*RESET_UNIT_SPI_CNTL &= ~BIT(1);
	}
}
