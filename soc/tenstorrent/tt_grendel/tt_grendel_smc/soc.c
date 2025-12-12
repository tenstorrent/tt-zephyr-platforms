/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#define RESET_UNIT_PERIPHERAL_RESETS_REG_ADDR (0xC00020A0)

typedef struct {
	uint32_t i3c_reset_n_n0_scan: 1;
	uint32_t rsvd_0: 3;
	uint32_t pvt_reset_n_n0_scan: 1;
	uint32_t rsvd_1: 3;
	uint32_t avs_reset_n_n0_scan: 1;
	uint32_t rsvd_2: 3;
	uint32_t i2c_reset_n_n0_scan: 1;
	uint32_t rsvd_3: 3;
	uint32_t uart_reset_n_n0_scan: 1;
	uint32_t rsvd_4: 3;
	uint32_t ptp_reset_n_n0_scan: 1;
	uint32_t rsvd_5: 3;
	uint32_t telemetry_reset_n_n0_scan: 1;
} SMC_WRAP_RESET_UNIT_MASTER_PERIPHERAL_RESETS_reg_t;

typedef union {
	uint32_t val;
	SMC_WRAP_RESET_UNIT_MASTER_PERIPHERAL_RESETS_reg_t f;
} SMC_WRAP_RESET_UNIT_MASTER_PERIPHERAL_RESETS_reg_u;

#define UART_WRAP0_UART_CTRL_REG_ADDR (0xC000A200)
#define UART_WRAP1_UART_CTRL_REG_ADDR (0xC000A600)
#define UART_WRAP2_UART_CTRL_REG_ADDR (0xC000AA00)
#define UART_WRAP3_UART_CTRL_REG_ADDR (0xC000AE00)

typedef struct {
	uint32_t uart_en: 1;
	uint32_t rsvd_0: 7;
	uint32_t uart_reset_n_n0_scan: 1;
	uint32_t rsvd_1: 7;
	uint32_t uart_clock_gate_en: 1;
} UART_CTRL_reg_t;

typedef union {
	uint32_t val;
	UART_CTRL_reg_t f;
} UART_CTRL_reg_u;

void uart_init(uint32_t base_addr)
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
