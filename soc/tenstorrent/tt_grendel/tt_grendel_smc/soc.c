/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#include "smc_cpu_reg.h"

static void uart_init(uint32_t base_addr)
{
	UART_CTRL_reg_u uart_ctrl;

	uart_ctrl.val = sys_read32(base_addr);
	uart_ctrl.f.uart_en = 1;
	uart_ctrl.f.uart_reset_n_n0_scan = 1;
	sys_write32(uart_ctrl.val, base_addr);
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

	sys_write32(reset_reg.val, RESET_UNIT_PERIPHERAL_RESETS_REG_ADDR);

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart0))) {
		/* Enable UART0 */
		uart_init(UART_WRAP0_UART_CTRL_REG_ADDR);
	}
	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart1))) {
		/* Enable UART1 */
		uart_init(UART_WRAP1_UART_CTRL_REG_ADDR);
	}

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart2))) {
		/* Enable UART2 */
		uart_init(UART_WRAP2_UART_CTRL_REG_ADDR);
	}

	if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart3))) {
		/* Enable UART3 */
		uart_init(UART_WRAP3_UART_CTRL_REG_ADDR);
	}
}
