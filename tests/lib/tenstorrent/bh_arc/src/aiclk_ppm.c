/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "aiclk_ppm.h"

static uint32_t fmax;
static uint32_t fmin;

static void *aiclk_ppm_setup(void)
{
	fmax = GetAiclkFmax();
	fmin = GetAiclkFmin();

	zassert_not_equal(fmin, fmax, "Fmin and Fmax values should not be equal");

	return NULL;
}

static void reset_arb(void *fixture)
{
	(void)fixture;

	/* Reset all arbiter values and disable */
	for (int i = 0; i < kAiclkArbMaxCount; i++) {
		SetAiclkArbMax(i, fmax);
		EnableArbMax(i, false);
	}
	for (int i = 0; i < kAiclkArbMinCount; i++) {
		SetAiclkArbMin(i, fmin);
		EnableArbMin(i, false);
	}
}

ZTEST(aiclk_ppm, test_no_arb_enabled)
{
	uint32_t targ_freq;

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(
		targ_freq, fmin,
		"Target frequency (%d) should be equal to Fmin (%d) when no arbiters are enabled",
		targ_freq, fmin);
}

ZTEST(aiclk_ppm, test_arb_min_disable_enable)
{
	uint32_t mod_fmin = fmin + 100;
	uint32_t targ_freq;

	/* Increase fmin arbiter */
	/* This should limit target aiclk to modified fmin when arbiter is enabled */
	SetAiclkArbMin(kAiclkArbMinFmin, mod_fmin);

	EnableArbMin(kAiclkArbMinFmin, false);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(targ_freq, fmin,
		      "Target frequency (%d) should be equal to Fmin (%d) when "
		      "Fmin arbiter is disabled",
		      targ_freq, fmin);

	EnableArbMin(kAiclkArbMinFmin, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, mod_fmin,
		      "Target frequency (%d) should be equal to modified Fmin (%d) when "
		      "arbiter enabled",
		      targ_freq, mod_fmin);
}

ZTEST(aiclk_ppm, test_arb_max_disable_enable)
{
	uint32_t mod_fmax = (fmin + fmax) / 2;
	uint32_t targ_freq;

	/* Set busy arbiter (kAiclkArbMinBusy = fmax [1400]) */
	/* Set fmax arbiter to value in between fmin and fmax [800] */
	/* This should limit target aiclk to modified fmax when both arbiters are enabled */
	aiclk_set_busy(true);
	SetAiclkArbMax(kAiclkArbMaxFmax, mod_fmax);

	EnableArbMin(kAiclkArbMinBusy, false);
	EnableArbMax(kAiclkArbMaxFmax, false);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(targ_freq, fmin,
		      "Target frequency (%d) should be equal to Fmin (%d) when "
		      "Fmax arbiter and Busy arbiter is disabled",
		      targ_freq, fmin);

	EnableArbMin(kAiclkArbMinBusy, true);
	EnableArbMax(kAiclkArbMaxFmax, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, mod_fmax,
		      "Target frequency (%d) should be equal to modified Fmax (%d) when "
		      "arbiter enabled",
		      targ_freq, mod_fmax);
}

ZTEST(aiclk_ppm, test_arb_freq_clamping)
{
	uint32_t targ_freq;

	/* Try setting min arbiter above fmax */
	SetAiclkArbMin(kAiclkArbMinFmin, fmax + 100);
	EnableArbMin(kAiclkArbMinFmin, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_true(targ_freq >= fmin && targ_freq <= fmax,
		     "Target frequency (%d) should be clamped within [%d, %d]", targ_freq, fmin,
		     fmax);

	EnableArbMin(kAiclkArbMinFmin, false);

	/* Try setting max arbiter below fmin */
	SetAiclkArbMax(kAiclkArbMaxFmax, fmin - 100);
	EnableArbMax(kAiclkArbMaxFmax, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_true(targ_freq >= fmin && targ_freq <= fmax,
		     "Target frequency (%d) should be clamped within [%d, %d]", targ_freq, fmin,
		     fmax);
}

ZTEST(aiclk_ppm, test_arb_lowest_max)
{
	uint32_t targ_freq;
	uint32_t expected_max;

	/* Set a high min arbiter */
	aiclk_set_busy(true);
	EnableArbMin(kAiclkArbMinBusy, true);

	/* Set multiple max arbiters to different values */
	SetAiclkArbMax(kAiclkArbMaxFmax, fmax - 100);
	EnableArbMax(kAiclkArbMaxFmax, true);

	SetAiclkArbMax(kAiclkArbMaxTDP, fmax - 200);
	EnableArbMax(kAiclkArbMaxTDP, true);

	SetAiclkArbMax(kAiclkArbMaxThm, fmax - 150);
	EnableArbMax(kAiclkArbMaxThm, true);

	expected_max = fmax - 200;

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, expected_max,
		      "Target frequency (%d) should be equal to lowest max arbiter (%d)", targ_freq,
		      expected_max);
}

ZTEST(aiclk_ppm, test_arb_highest_min)
{
	uint32_t targ_freq;
	uint32_t expected_min;

	/* Set multiple min arbiters to different values */
	SetAiclkArbMin(kAiclkArbMinFmin, fmin + 100);
	EnableArbMin(kAiclkArbMinFmin, true);

	SetAiclkArbMin(kAiclkArbMinBusy, fmin + 200);
	EnableArbMin(kAiclkArbMinBusy, true);

	expected_min = fmin + 200;

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, expected_min,
		      "Target frequency (%d) should be equal to highest min arbiter (%d)",
		      targ_freq, expected_min);
}

ZTEST(aiclk_ppm, test_max_arb_less_than_fmin)
{
	uint32_t targ_freq;

	/* Set fmax arbiter below fmin */
	SetAiclkArbMax(kAiclkArbMaxFmax, fmin - 100);
	EnableArbMax(kAiclkArbMaxFmax, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(
		targ_freq, fmin,
		"Target frequency (%d) should be equal to Fmin (%d) when max arbiter is below Fmin",
		targ_freq, fmin);
}

ZTEST(aiclk_ppm, test_min_arb_greater_than_max_arb)
{
	uint32_t targ_freq;
	uint32_t min_arb_value = fmax - 100;
	uint32_t max_arb_value = fmin + 100;

	/* Set fmin arbiter above fmax arbiter */
	SetAiclkArbMin(kAiclkArbMinFmin, min_arb_value);
	EnableArbMin(kAiclkArbMinFmin, true);

	SetAiclkArbMax(kAiclkArbMaxFmax, max_arb_value);
	EnableArbMax(kAiclkArbMaxFmax, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, max_arb_value,
		      "Target frequency (%d) should be equal to max arbiter value (%d) when min "
		      "arbiter is above max arbiter",
		      targ_freq, fmin);
}

ZTEST_SUITE(aiclk_ppm, NULL, aiclk_ppm_setup, reset_arb, NULL, NULL);
