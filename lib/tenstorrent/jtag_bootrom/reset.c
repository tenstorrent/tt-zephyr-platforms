/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/jtag_bootrom.h>
#include <tenstorrent/bh_chip.h>

#include <zephyr/kernel.h>

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

  int ret = jtag_bootrom_reset_asic(chip);
  if (ret) {
    return ret;
  }

	if (ret) {
		return ret;
	}

  jtag_bootrom_patch_offset(chip, patch, patch_len, 0x80);

  if (jtag_bootrom_verify(chip->config.jtag, patch, patch_len) != 0) {
    printk("Bootrom verification failed\n");
  }

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

	return 0;
}
