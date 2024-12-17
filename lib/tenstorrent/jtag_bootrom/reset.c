/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/jtag_bootrom.h>
#include <tenstorrent/bh_chip.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(jtag_bootrom, CONFIG_TT_JTAG_BOOTROM_LOG_LEVEL);

__aligned(sizeof(uint32_t)) static const uint8_t bootcode[] = {
#include "bootcode.h"
};

/* discarded if no zephyr,gpio-emul exists or if CONFIG_JTAG_VERIFY_WRITE=n */
__aligned(sizeof(uint32_t)) static uint8_t sram[sizeof(bootcode)];

const uint8_t *get_bootcode(void)
{
	return bootcode;
}

const size_t get_bootcode_len(void)
{
	return sizeof(bootcode) / sizeof(uint32_t);
}

int jtag_bootrom_reset_sequence(struct bh_chip *chip, bool force_reset)
{
	const uint32_t *const patch = (const uint32_t *)bootcode;
	const size_t patch_len = get_bootcode_len();

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
	if (force_reset) {
		chip->data.needs_reset = true;
	}
#endif

	int64_t start = k_uptime_get();

	int ret = jtag_bootrom_reset_asic(chip);

	if (ret) {
		return ret;
	}

	if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul) && IS_ENABLED(CONFIG_JTAG_VERIFY_WRITE)) {
		jtag_bootrom_emul_setup((uint32_t *)sram, patch_len);
	}

	jtag_bootrom_patch_offset(chip, patch, patch_len, 0x80);

	volatile int64_t end = k_uptime_delta(&start);

	LOG_DBG("jtag bootrom load took %lld ms", end);

	if (jtag_bootrom_verify(chip->config.jtag, patch, patch_len) != 0) {
		printk("Bootrom verification failed\n");
	}

	start = k_uptime_get();

	bh_chip_cancel_bus_transfer_set(chip);
#ifdef CONFIG_JTAG_LOAD_ON_PRESET
	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);
	if (chip->data.needs_reset) {
		jtag_bootrom_soft_reset_arc(chip);
	}
	k_mutex_unlock(&chip->data.reset_lock);
#else
	jtag_bootrom_soft_reset_arc(chip);
#endif
	bh_chip_cancel_bus_transfer_clear(chip);

	jtag_bootrom_teardown(chip);

	end = k_uptime_delta(&start);
	LOG_DBG("jtag bootrom reset took %lld ms", end);

	return 0;
}
