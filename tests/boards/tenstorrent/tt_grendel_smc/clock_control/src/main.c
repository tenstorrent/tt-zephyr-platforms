/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/printk.h>

/*
 * The CGM driver uses the clock_control_subsys_t parameter as the output
 * clock index (0-3).
 */
#define CGM_OUTPUT_CLK0 ((clock_control_subsys_t)0)
#define CGM_OUTPUT_CLK1 ((clock_control_subsys_t)1)
#define CGM_OUTPUT_CLK2 ((clock_control_subsys_t)2)
#define CGM_OUTPUT_CLK3 ((clock_control_subsys_t)3)

/* Invalid output index for negative testing */
#define CGM_OUTPUT_INVALID ((clock_control_subsys_t)5)

static void clock_control_rate_before(void *fixture)
{
	ARG_UNUSED(fixture);

	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));

	zassert_true(device_is_ready(cgm));

	for (uint8_t output = 0; output < 4; output++) {
		int ret = clock_control_on(cgm, (clock_control_subsys_t)(uintptr_t)output);

		zassert_ok(ret, "clock_control_on for output %u failed with %d", output, ret);
	}
}

ZTEST(clock_control_rate, test_get_rate_invalid_output_fails)
{
	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));
	uint32_t clock_rate;
	int ret;

	ret = clock_control_get_rate(cgm, CGM_OUTPUT_INVALID, &clock_rate);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL for invalid output, got %d", ret);
}

ZTEST(clock_control_rate, test_set_rate_output0)
{
	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));
	uint32_t new_rate;
	int ret;

	for (uint32_t target_rate = CONFIG_CLOCK_CTRL_CGM_MIN_RATE;
	     target_rate <= (uint32_t)CONFIG_CLOCK_CTRL_CGM_MAX_RATE;
	     target_rate += CONFIG_CLOCK_CTRL_CGM_FREQ_INCR) {
		ret = clock_control_set_rate(cgm, CGM_OUTPUT_CLK0,
					     (clock_control_subsys_rate_t)target_rate);
		zassert_ok(ret, "set_rate to %u Hz failed with %d", target_rate, ret);

		ret = clock_control_get_rate(cgm, CGM_OUTPUT_CLK0, &new_rate);
		zassert_ok(ret, "get_rate after set_rate failed with %d", ret);

		uint32_t tolerance = (uint32_t)((uint64_t)target_rate *
						CONFIG_CLOCK_CTRL_TOLERANCE_PERCENT / 100);
		zassert_within(new_rate, target_rate, tolerance, "Expected %u Hz but got %u Hz",
			       target_rate, new_rate);

		printk("CGM0 set_rate: target=%u Hz actual=%u Hz\n", target_rate, new_rate);
	}
}

ZTEST(clock_control_rate, test_set_rate_zero_fails)
{
	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));
	int ret;

	ret = clock_control_set_rate(cgm, CGM_OUTPUT_CLK0, (clock_control_subsys_rate_t)0);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL for zero rate, got %d", ret);
}

ZTEST(clock_control_rate, test_set_rate_invalid_output_fails)
{
	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));
	int ret;

	ret = clock_control_set_rate(cgm, CGM_OUTPUT_INVALID,
				     (clock_control_subsys_rate_t)200000000);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL for invalid output, got %d", ret);
}

ZTEST_SUITE(clock_control_rate, NULL, NULL, clock_control_rate_before, NULL, NULL);

ZTEST(clock_control_on_off, test_off_output0)
{
	const struct device *cgm = DEVICE_DT_GET(DT_NODELABEL(cgm0));
	int ret;

	zassert_true(device_is_ready(cgm));

	ret = clock_control_on(cgm, CGM_OUTPUT_CLK0);
	zassert_ok(ret, "clock_control_on failed with %d", ret);

	ret = clock_control_off(cgm, CGM_OUTPUT_CLK0);
	zassert_ok(ret, "clock_control_off for output 0 failed with %d", ret);
}

ZTEST_SUITE(clock_control_on_off, NULL, NULL, NULL, NULL, NULL);
