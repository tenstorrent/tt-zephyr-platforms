/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <tenstorrent/jtag_bootrom.h>
#include <zephyr/ztest.h>

ZTEST(jtag_bootrom, test_jtag_bootrom)
{
  const uint32_t *const patch = (const uint32_t *)get_bootcode();
  const size_t patch_len = get_bootcode_len();

  zassert_ok(jtag_bootrom_patch(patch, patch_len));
  zassert_ok(jtag_bootrom_verify(patch, patch_len));
}

static void before(void *arg)
{
  ARG_UNUSED(arg);

  /* discarded if no zephyr,gpio-emul exists or if CONFIG_JTAG_VERIFY_WRITE=n */
  __aligned(sizeof(uint32_t))
  uint8_t *sram = malloc(get_bootcode_len() * sizeof(uint8_t));
  const size_t patch_len = get_bootcode_len();

  zassert_ok(jtag_bootrom_setup());

  if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul) && IS_ENABLED(CONFIG_JTAG_VERIFY_WRITE)) {
    jtag_bootrom_emul_setup((uint32_t *)sram, patch_len);
  }
}

static void after(void *arg)
{
  ARG_UNUSED(arg);

  jtag_bootrom_teardown();
}

ZTEST_SUITE(jtag_bootrom, NULL, NULL, before, after, NULL);
