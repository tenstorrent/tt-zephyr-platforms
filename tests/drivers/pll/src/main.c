/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/printk.h>

ZTEST(pll_basic, test_pll_init)
{
	const struct device *pll = DEVICE_DT_GET(DT_NODELABEL(pll));

	zassert_true(device_is_ready(pll));
}

ZTEST_SUITE(pll_basic, NULL, NULL, NULL, NULL, NULL);
