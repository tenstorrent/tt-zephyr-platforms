/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file hotload.c
 * @brief Hotload handling implementation
 *
 */

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>

#define GLOBAL_RESET 0x80030000
#define ETH_RESET    0x80030008
#define DDR_RESET    0x80030010
#define I2C_CNTL     0x800300f0
#define SPI_CNTL     0x800300f8

#define DT_DRV_COMPAT         tenstorrent_bh_clock_control
#define PLL_DEVICE_INIT(inst) DEVICE_DT_INST_GET(inst),

static const struct device *const pll_devs[] = {DT_INST_FOREACH_STATUS_OKAY(PLL_DEVICE_INIT)};

static uint8_t hotload_handler(const union request *request, struct response *response)
{
	struct arc_vector_table {
		void (*reset)(void); /* Reset vector */
	} *vt;
	vt = (struct arc_vector_table *)request->data[1];

	/*
	 * TODO- it is unclear if we need to reset more here. For now,
	 * these resets are sufficient to get the hotload working with
	 * mission mode CMFW
	 */

	/* Resets need to be asserted at slow clock speed. Bypass PLLs */
	ARRAY_FOR_EACH(pll_devs, i) {
		clock_control_configure(pll_devs[i], NULL,
					(void *)CLOCK_CONTROL_TT_BH_CONFIG_BYPASS);
	}
	/* First, reset the PCIe complex */
	uint32_t reg = sys_read32(GLOBAL_RESET);
	/* Clear noc and pcie reset */
	reg &= ~(BIT(1) | BIT(8));
	sys_write32(reg, GLOBAL_RESET);
	/* Reset ETH */
	reg = sys_read32(ETH_RESET);
	reg = 0x0;
	sys_write32(reg, ETH_RESET);
	/* Reset DDR */
	reg = sys_read32(DDR_RESET);
	reg = 0x0;
	sys_write32(reg, DDR_RESET);
	/* Reset SPI */
	reg = sys_read32(SPI_CNTL);
	reg |= BIT(4); /* SPI reset */
	sys_write32(reg, SPI_CNTL);
	k_msleep(5);
	/* Deassert SPI reset */
	reg &= ~BIT(4);
	sys_write32(reg, SPI_CNTL);
	/* Assert I2C reset */
	reg = sys_read32(I2C_CNTL);
	reg |= BIT(4); /* I2C reset */
	sys_write32(reg, I2C_CNTL);
	k_msleep(5);
	/* Deassert I2C reset */
	reg &= ~BIT(4);
	sys_write32(reg, I2C_CNTL);
	/* Now we are ready to jump to new firmware. Set PC, and go */
	irq_lock();

	vt->reset();
	/* Should never get here */
	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_HOTLOAD, hotload_handler);
