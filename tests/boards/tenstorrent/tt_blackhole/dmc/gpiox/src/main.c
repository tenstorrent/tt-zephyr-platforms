/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

/* max number of GPIOs supported by ti,tca9554a (or nxp,pca95xx) */
#define NGPIOS 8

static const struct device *const ports[] = {
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox0)), DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox1)),
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox2)), DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox3)),
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox4)), DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpiox5)),
};

/* clang-format off */
static const bool expected_availability[] = {
#ifdef CONFIG_BOARD_REVISION_P100
	true, false, true, true, true, true
#endif
#if defined(CONFIG_BOARD_REVISION_P100A) || defined(CONFIG_BOARD_REVISION_P150A) ||                \
	defined(CONFIG_BOARD_REVISION_P150B) || defined(CONFIG_BOARD_REVISION_P150C) ||            \
	defined(CONFIG_BOARD_REVISION_P300A) || defined(CONFIG_BOARD_REVISION_P300B) ||            \
	defined(CONFIG_BOARD_REVISION_P300C)
	true, true, true, true, false, false,
#endif
	/* may be of zero size for 3rd-party boards so that tests will be skipped / pass */
};
/* clang-format on */

BUILD_ASSERT(ARRAY_SIZE(expected_availability) <= ARRAY_SIZE(ports),
	     "expected_availability exceeds ports size");

static void test_gpiox_common(int i)
{
	const struct device *const dev = ports[i];

	if (i >= ARRAY_SIZE(expected_availability)) {
		ztest_test_skip();
	}

	zassert_false(expected_availability[i] ^ (dev != NULL), "port %d should be %s", i,
		      expected_availability[i] ? "available" : "unavailable");

	if (dev == NULL) {
		ztest_test_skip();
	}

	for (int j = 0; j < NGPIOS; j++) {
		int ret;
		gpio_flags_t flags;

		/*
		 * We intentionally do not change the GPIO configuration to avoid any adverse
		 * side-effects. This test is mainly checking that we can communicate with the GPIO
		 * expanders for each pin that exists.
		 */
		ret = gpio_pin_get_config(dev, j, &flags);
		zassert_ok(ret, "failed to get gpio config for port %d, pin %d: %d", i, j, ret);
	}
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox0)
{
	test_gpiox_common(0);
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox1)
{
	test_gpiox_common(1);
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox2)
{
	test_gpiox_common(2);
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox3)
{
	test_gpiox_common(3);
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox4)
{
	test_gpiox_common(4);
}

ZTEST(tt_blackhole_dmc_gpiox, test_gpiox5)
{
	test_gpiox_common(5);
}

ZTEST_SUITE(tt_blackhole_dmc_gpiox, NULL, NULL, NULL, NULL, NULL);
