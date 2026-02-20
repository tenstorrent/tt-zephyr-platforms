/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#include "smc_cpu_reg.h"

static void uart_x_init(uint32_t base_addr)
{
	UART_CTRL_reg_u uart_ctrl;

	uart_ctrl.val = sys_read32(base_addr);
	uart_ctrl.f.uart_en = 1;
	uart_ctrl.f.uart_reset_n_n0_scan = 1;
	sys_write32(uart_ctrl.val, base_addr);
}

static void i3c_x_init(uint32_t i3c_base_addr)
{
	I3C_CTRL_I3C_RESET_CTRL_STATUS_reg_u i3c_ctrl;
	I3C_CTRL_PINSTRAPS_GROUP_1A_reg_u i3c_pinstraps;

	/* Configure I3C role as controller */
	i3c_pinstraps.val = sys_read32(i3c_base_addr +
			I3C_WRAP_0_I3C_CTRL_PINSTRAPS_GROUP_1A_REG_OFFSET);
	i3c_pinstraps.f.device_role = 0x0; /* Primary Controller */
	sys_write32(i3c_pinstraps.val,
		i3c_base_addr + I3C_WRAP_0_I3C_CTRL_PINSTRAPS_GROUP_1A_REG_OFFSET);

	/* Enable I3Cx */
	i3c_ctrl.val = sys_read32(i3c_base_addr +
				  I3C_WRAP_0_I3C_CTRL_I3C_RESET_CTRL_STATUS_REG_OFFSET);
	i3c_ctrl.f.i3c_reset_n_n0_scan = 1;
	i3c_ctrl.f.reg_reset_n_n0_scan = 1;
	i3c_ctrl.f.i3c_enable_n0_scan = 1;
	sys_write32(i3c_ctrl.val, i3c_base_addr +
		    I3C_WRAP_0_I3C_CTRL_I3C_RESET_CTRL_STATUS_REG_OFFSET);
}

void uart_init(void)
{
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart0))) {
		/* Enable UART0 */
		uart_x_init(UART_WRAP0_UART_CTRL_REG_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart1))) {
		/* Enable UART1 */
		uart_x_init(UART_WRAP1_UART_CTRL_REG_ADDR);
	}

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart2))) {
		/* Enable UART2 */
		uart_x_init(UART_WRAP2_UART_CTRL_REG_ADDR);
	}

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart3))) {
		/* Enable UART3 */
		uart_x_init(UART_WRAP3_UART_CTRL_REG_ADDR);
	}
}


void i3c_init(void)
{
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c0))) {
		/* Enable I3C0 */
		i3c_x_init(I3C_WRAP_0_I3C_CTRL_REG_MAP_BASE_ADDR);
	}

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c1))) {
		/* Enable I3C1 */
		i3c_x_init(I3C_WRAP_1_I3C_CTRL_REG_MAP_BASE_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c2))) {
		/* Enable I3C2 */
		i3c_x_init(I3C_WRAP_2_I3C_CTRL_REG_MAP_BASE_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c3))) {
		/* Enable I3C3 */
		i3c_x_init(I3C_WRAP_3_I3C_CTRL_REG_MAP_BASE_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c4))) {
		/* Enable I3C4 */
		i3c_x_init(I3C_WRAP_4_I3C_CTRL_REG_MAP_BASE_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(i3c5))) {
		/* Enable I3C5 */
		i3c_x_init(I3C_WRAP_5_I3C_CTRL_REG_MAP_BASE_ADDR);
	}
}

/**
 * @brief Early hardware init hook
 */
void soc_early_init_hook(void)
{
	SMC_WRAP_RESET_UNIT_MASTER_PERIPHERAL_RESETS_reg_u reset_reg;

	reset_reg.val = sys_read32(RESET_UNIT_PERIPHERAL_RESETS_REG_ADDR);

	/* Take peripherals out of reset */
	reset_reg.f.uart_reset_n_n0_scan = DT_HAS_COMPAT_STATUS_OKAY(ns16550);
	reset_reg.f.i3c_reset_n_n0_scan = DT_HAS_COMPAT_STATUS_OKAY(cdns_i3c);

	sys_write32(reset_reg.val, RESET_UNIT_PERIPHERAL_RESETS_REG_ADDR);

	uart_init();
	i3c_init();
}
