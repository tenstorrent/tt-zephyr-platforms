/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/ztest.h>

extern uint32_t fan_curve(float max_asic_temp, float max_gddr_temp);

ZTEST(fan_ctrl, test_fan_curve)
{
	/* Test GDDR temp fan curve steps */
	zassert_equal(fan_curve(25, 25), 35);
	zassert_equal(fan_curve(25, 30), 35);
	zassert_equal(fan_curve(25, 35), 35);
	zassert_equal(fan_curve(25, 40), 35);
	zassert_equal(fan_curve(25, 45), 35);
	zassert_equal(fan_curve(25, 50), 37);
	zassert_equal(fan_curve(25, 55), 41);
	zassert_equal(fan_curve(25, 60), 47);
	zassert_equal(fan_curve(25, 65), 55);
	zassert_equal(fan_curve(25, 70), 66);
	zassert_equal(fan_curve(25, 75), 78);
	zassert_equal(fan_curve(25, 80), 93);
	zassert_equal(fan_curve(25, 85), 100);
	zassert_equal(fan_curve(25, 90), 100);

	/* Test ASIC temp fan curve steps */
	zassert_equal(fan_curve(25, 25), 35);
	zassert_equal(fan_curve(30, 25), 35);
	zassert_equal(fan_curve(35, 25), 35);
	zassert_equal(fan_curve(40, 25), 35);
	zassert_equal(fan_curve(45, 25), 35);
	zassert_equal(fan_curve(50, 25), 35);
	zassert_equal(fan_curve(55, 25), 36);
	zassert_equal(fan_curve(60, 25), 39);
	zassert_equal(fan_curve(65, 25), 44);
	zassert_equal(fan_curve(70, 25), 52);
	zassert_equal(fan_curve(75, 25), 61);
	zassert_equal(fan_curve(80, 25), 72);
	zassert_equal(fan_curve(85, 25), 85);
	zassert_equal(fan_curve(90, 25), 100);

	/* Test boundary conditions */
	static const float temps[] = {
		-INFINITY, /* negative-most condition */
		-35,       /* darn cold */
		-1,        /* on the boundary */
		0,         /* inflection point */
		1,         /* on the boundary */
		23,        /* ~room temp */
		50,        /* pretty warm */
		100,       /* hot! */
		300,       /* on fire */
		INFINITY,  /* positive-most condition */
	};

	for (size_t i = 0; i < ARRAY_SIZE(temps); ++i) {
		for (size_t j = 0; j < ARRAY_SIZE(temps); ++j) {
			uint32_t pct = fan_curve(temps[i], temps[j]);

			zassert_true(pct >= 0, "unexpected pct %u for fan_curve(%f, %f)");
			zassert_true(pct <= 100, "unexpected pct %u for fan_curve(%f, %f)");
		}
	}
}

ZTEST_SUITE(fan_ctrl, NULL, NULL, NULL, NULL, NULL);
