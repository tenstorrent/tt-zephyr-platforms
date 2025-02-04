/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <math.h>

/*
 * FIXME: need to separate platform-layer (arch) code, kernel-layer (driver) code,
 * and application-layer (library/telemetry) code in bh_arc lib, i.e. separate
 * the information processing from the hardware specifics.
 */
#include "../../../../../lib/tenstorrent/bh_arc/fan_ctrl.c"

extern uint32_t fan_curve(float max_asic_temp, float max_gddr_temp);

ZTEST(bh_arc, test_fan_curve)
{
	/* Test each GDDR temp fan curve step */
	zassert_equal(fan_curve(25, 25), 35);
	zassert_equal(fan_curve(25, 42), 35);
	zassert_equal(fan_curve(25, 46), 40);
	zassert_equal(fan_curve(25, 52), 45);
	zassert_equal(fan_curve(25, 59), 50);
	zassert_equal(fan_curve(25, 64), 55);
	zassert_equal(fan_curve(25, 68), 60);
	zassert_equal(fan_curve(25, 71), 65);
	zassert_equal(fan_curve(25, 74), 70);
	zassert_equal(fan_curve(25, 77), 90);
	zassert_equal(fan_curve(25, 80), 100);

	/* Test each ASIC temp fan curve step */
	zassert_equal(fan_curve(25, 25), 35);
	zassert_equal(fan_curve(46, 25), 35);
	zassert_equal(fan_curve(52, 25), 40);
	zassert_equal(fan_curve(56, 25), 45);
	zassert_equal(fan_curve(60, 25), 50);
	zassert_equal(fan_curve(65, 25), 55);
	zassert_equal(fan_curve(70, 25), 60);
	zassert_equal(fan_curve(74, 25), 65);
	zassert_equal(fan_curve(80, 25), 70);
	zassert_equal(fan_curve(85, 25), 90);
	zassert_equal(fan_curve(92, 25), 100);

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

ZTEST_SUITE(bh_arc, NULL, NULL, NULL, NULL, NULL);
