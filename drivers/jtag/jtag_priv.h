/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_JTAG_JTAG_PRIV_H_
#define ZEPHYR_DRIVERS_JTAG_JTAG_PRIV_H_

#include <stdint.h>

#include <zephyr/drivers/gpio.h>

typedef uint32_t jtag_reg_t;

enum jtag_shift_reg {
	BR, /* Bypass Register */
	IR, /* Instruction Register */
	DR, /* Data Register */
};

enum jtag_state {
	/* Calling this RESET results in a naming collision */
	JTAG_RESET,
	IDLE,
	SCAN_DR,
	SCAN_IR,
	CAPTURE_DR,
	CAPTURE_IR,
	SHIFT_DR,
	SHIFT_IR,
	EXIT1_DR,
	EXIT1_IR,
	PAUSE_DR,
	PAUSE_IR,
	EXIT2_DR,
	EXIT2_IR,
	UPDATE_DR,
	UPDATE_IR,
};

struct jtag_emul_data {
	jtag_reg_t shift_reg[DR + 1];
	uint8_t shift_bits[DR + 1];
	jtag_reg_t hold_reg[DR + 1];
	enum jtag_state state;
	enum jtag_shift_reg selected_reg;
	bool tck_old;
	size_t tck_count;
	bool have_axi_addr_tdr;
	uint32_t axi_addr_tdr;
	bool have_axi_data_tdr;
	uint32_t axi_data_tdr;
	uint32_t *sram;
	size_t sram_len;
};

struct jtag_config {
	struct gpio_dt_spec tck;
	struct gpio_dt_spec tdo;
	struct gpio_dt_spec tdi;
	struct gpio_dt_spec tms;
	struct gpio_dt_spec trst;

	volatile uint32_t *tck_reg;
	volatile uint32_t *tdo_reg;
	volatile uint32_t *tdi_reg;
	volatile uint32_t *tms_reg;
	volatile uint32_t *trst_reg;

	uint32_t port_write_cycles;
	uint32_t tck_delay;
};

struct jtag_data {
#ifdef CONFIG_JTAG_EMUL
	struct gpio_dt_spec tck;
	struct gpio_dt_spec tdo;
	struct gpio_dt_spec tdi;
	struct gpio_dt_spec tms;
	struct gpio_dt_spec trst;

	uint32_t *buf;
	size_t buf_len;
	struct gpio_callback gpio_emul_cb;
	struct jtag_emul_data emul_data;
#endif
};

#endif
