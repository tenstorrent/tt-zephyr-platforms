/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/sys/libc-hooks.h>
#include <zephyr/device.h>
#include <zephyr/init.h>

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

static int tt_console_out(int character)
{
	static uint32_t prev_val;
	uint32_t val = (character & 0xFF) << 8;

	if (val == prev_val) {
		/* If the value is the same as the previous one, toggle the toggle bit */
		val ^= BIT(0);
	}
	WRITE_SCRATCH(2, val);
	prev_val = val;

	return character;
}

static int tt_virt_console_init(void)
{
	__printk_hook_install(tt_console_out);
	__stdout_hook_install(tt_console_out);

	return 0;
}

SYS_INIT(tt_virt_console_init, PRE_KERNEL_1, CONFIG_CONSOLE_INIT_PRIORITY);
