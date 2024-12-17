/*
 * Copyright (c) 2024, Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <tenstorrent/jtag_bootrom.h>

__aligned(sizeof(uint32_t)) static const uint8_t bootcode[] = {
#include "bootcode.h"
};

/* discarded if no zephyr,gpio-emul exists or if CONFIG_JTAG_VERIFY_WRITE=n */
__aligned(sizeof(uint32_t)) static uint8_t sram[sizeof(bootcode)];

const uint8_t *get_bootcode()
{
  return bootcode;
}

const size_t get_bootcode_len()
{
  return sizeof(bootcode) / sizeof(uint32_t);
}

int jtag_bootrom_reset(bool force_reset)
{
  const uint32_t *const patch = (const uint32_t *)bootcode;
  const size_t patch_len = get_bootcode_len();

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
  if (force_reset) {
    jtag_bootrom_force_reset();
  }
#endif

  int ret = jtag_bootrom_setup();
  if (ret) {
    return ret;
  }

  if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul) && IS_ENABLED(CONFIG_JTAG_VERIFY_WRITE)) {
    jtag_bootrom_emul_setup((uint32_t *)sram, patch_len);
  }

  jtag_bootrom_patch_offset(patch, patch_len, 0x80);

  if (jtag_bootrom_verify(patch, patch_len) != 0) {
    printk("Bootrom verification failed\n");
  }

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
  struct k_spinlock reset_lock = jtag_bootrom_reset_lock();
  K_SPINLOCK(&reset_lock) {
    if (jtag_bootrom_needs_reset()) {
      jtag_bootrom_soft_reset_arc();
    }
  }
#else
  jtag_bootrom_soft_reset_arc();
#endif

  jtag_bootrom_teardown();

  return 0;
}
