/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief Tenstorrent virtual console log backend implementation.
 *
 * Sends log messages to the Tenstorrent virtual console, used
 * in pre-silicon development environments.
 */

#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_core.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_backend_std.h>

#include <soc.h>

/*
 * Format of 32-bit writes to scratch2 for virtual console:
 * Byte order is little endian
 *
 * Upper 24 bits are payload
 * Lower 8 bits:
 *     [7:4] reserved, must be 0
 *     [3:1] opcode
 *     [0]   toggle bit, toggles to ensure every write to register is processed by environment
 *
 * Opcodes:
 * 0x0 : 24-bit payload is ASCII (lowest-order byte is first character)
 * 0x1 : 16-bit hex (little endian), presented as hex
 * 0x2 : 24-bit decimal, presented as decimal (not currently implemented)
 * 0x3-0x7 : reserved
 */

#define OPCODE_ASCII	0x0
#define OPCODE_HEX	0x1

static uint8_t buf[1];
static uint32_t log_format_current = CONFIG_LOG_BACKEND_TT_VIRT_OUTPUT_DEFAULT;


struct tt_virt_console_msg {
	uint32_t toggle: 1;
	uint32_t opcode: 3;
	uint32_t rsvd: 4;
	uint32_t payload: 24;
} __packed;

union tt_virt_console_reg {
	uint32_t val;
	struct tt_virt_console_msg msg;
};

static int tt_console_out(int character)
{
	static union tt_virt_console_reg reg;
	static union tt_virt_console_reg prev_reg;

	reg.msg.payload = (uint8_t)character;
	reg.msg.opcode = OPCODE_ASCII;

	if (reg.val == prev_reg.val) {
		/* If the value is the same as the previous one, toggle the toggle bit */
		reg.msg.toggle ^= 1;
	}
	WRITE_SCRATCH(2, reg.val);
	prev_reg.val = reg.val;
}

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	for (size_t i = 0; i < length; i++) {
		tt_console_out(data[i]);
	}

	return length;
}

LOG_OUTPUT_DEFINE(log_output_tt_virt, char_out, buf, sizeof(buf));

static void log_backend_tt_virt_process(const struct log_backend *const backend,
				    union log_msg_generic *msg)
{
	uint32_t flags = log_backend_std_get_flags();

	log_format_func_t log_output_func = log_format_func_t_get(log_format_current);

	log_output_func(&log_output_tt_virt, &msg->log, flags);
}

static int format_set(const struct log_backend *const backend, uint32_t log_type)
{
	log_format_current = log_type;
	return 0;
}

static void log_backend_tt_virt_init(struct log_backend const *const backend)
{
}

static void log_backend_tt_virt_panic(struct log_backend const *const backend)
{
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);

	log_backend_std_dropped(&log_output_tt_virt, cnt);
}

const struct log_backend_api log_backend_tt_virt_api = {
	.process = log_backend_tt_virt_process,
	.panic = log_backend_tt_virt_panic,
	.init = log_backend_tt_virt_init,
	.dropped = IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE) ? NULL : dropped,
	.format_set = format_set,
};

LOG_BACKEND_DEFINE(log_backend_tt_virt, log_backend_tt_virt_api, true);
