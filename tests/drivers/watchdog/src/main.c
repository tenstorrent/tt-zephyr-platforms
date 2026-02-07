/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

const struct device *wdog = DEVICE_DT_GET_OR_NULL(DT_ALIAS(wdog));

ZTEST_SUITE(wdog, NULL, NULL, NULL, NULL, NULL);

K_SEM_DEFINE(wdog_sem, 0, 1);

static void wdt_callback(const struct device *wdt_dev, int channel_id)
{
	wdt_feed(wdt_dev, channel_id);
	k_sem_give(&wdog_sem);
}

ZTEST(wdog, test_cb)
{
	int ret;
	struct wdt_timeout_cfg wdt_config = {
		.flags = WDT_FLAG_RESET_NONE,
		.window.min = 0,
		.window.max = 10,
		.callback = wdt_callback,
	};

	zassert_true(device_is_ready(wdog));

	TC_PRINT("Configuring watchdog\n");
	ret = wdt_install_timeout(wdog, &wdt_config);
	zassert_equal(ret, 0);
	ret = wdt_setup(wdog, 0);
	zassert_equal(ret, 0);

	TC_PRINT("Awaiting callback\n");
	ret = k_sem_take(&wdog_sem, K_MSEC(100));
	zassert_equal(ret, 0);
}
