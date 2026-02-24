/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#include "smc_cpu_reg.h"

#define UART_INIT_IF_OKAY(n)                                                                       \
	do {                                                                                       \
		if (DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(uart##n))) {                              \
			uart_init(UART_WRAP##n##_UART_CTRL_REG_ADDR);                              \
		}                                                                                  \
	} while (false)

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

	UART_INIT_IF_OKAY(0);
	UART_INIT_IF_OKAY(1);
	UART_INIT_IF_OKAY(2);
	UART_INIT_IF_OKAY(3);
}
