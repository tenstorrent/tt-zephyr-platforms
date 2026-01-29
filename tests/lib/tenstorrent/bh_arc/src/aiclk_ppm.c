/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "aiclk_ppm.h"
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/smc_msg.h>
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
	for (int i = 0; i < aiclk_arb_max_count; i++) {
		SetAiclkArbMax(i, fmax);
		EnableArbMax(i, false);
	}
	for (int i = 0; i < aiclk_arb_min_count; i++) {
		SetAiclkArbMin(i, fmin);
		EnableArbMin(i, false);
	}
}

static void set_busy(bool busy)
{
	union request req = {0};
	struct response rsp = {0};

	req.aiclk_set_speed.command_code =
		busy ? TT_SMC_MSG_AICLK_GO_BUSY : TT_SMC_MSG_AICLK_GO_LONG_IDLE;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);
	zexpect_equal(rsp.data[0], 0);
}

static void reinit_arb(void *fixture)
{
	(void)fixture;
	for (int i = 0; i < aiclk_arb_max_count; i++) {
		SetAiclkArbMax(i, fmax);
		EnableArbMax(i, true);
	}
	for (int i = 0; i < aiclk_arb_min_count; i++) {
		SetAiclkArbMin(i, fmin);
		EnableArbMin(i, true);
	}

	set_busy(false);
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
	SetAiclkArbMin(aiclk_arb_min_fmin, mod_fmin);

	EnableArbMin(aiclk_arb_min_fmin, false);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(targ_freq, fmin,
		      "Target frequency (%d) should be equal to Fmin (%d) when "
		      "Fmin arbiter is disabled",
		      targ_freq, fmin);

	EnableArbMin(aiclk_arb_min_fmin, true);

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

	/* Set busy arbiter (aiclk_arb_min_busy = fmax [1400]) */
	/* Set fmax arbiter to value in between fmin and fmax [800] */
	/* This should limit target aiclk to modified fmax when both arbiters are enabled */
	set_busy(true);
	SetAiclkArbMax(aiclk_arb_max_fmax, mod_fmax);

	EnableArbMin(aiclk_arb_min_busy, false);
	EnableArbMax(aiclk_arb_max_fmax, false);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(targ_freq, fmin,
		      "Target frequency (%d) should be equal to Fmin (%d) when "
		      "Fmax arbiter and Busy arbiter is disabled",
		      targ_freq, fmin);

	EnableArbMin(aiclk_arb_min_busy, true);
	EnableArbMax(aiclk_arb_max_fmax, true);

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
	SetAiclkArbMin(aiclk_arb_min_fmin, fmax + 100);
	EnableArbMin(aiclk_arb_min_fmin, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_true(targ_freq >= fmin && targ_freq <= fmax,
		     "Target frequency (%d) should be clamped within [%d, %d]", targ_freq, fmin,
		     fmax);

	EnableArbMin(aiclk_arb_min_fmin, false);

	/* Try setting max arbiter below fmin */
	SetAiclkArbMax(aiclk_arb_max_fmax, fmin - 100);
	EnableArbMax(aiclk_arb_max_fmax, true);

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
	enum aiclk_arb_max effective_max_arb;

	/* Set a high min arbiter */

	set_busy(true);
	EnableArbMin(aiclk_arb_min_busy, true);

	/* Set multiple max arbiters to different values */
	SetAiclkArbMax(aiclk_arb_max_fmax, fmax - 100);
	EnableArbMax(aiclk_arb_max_fmax, true);

	SetAiclkArbMax(aiclk_arb_max_tdp, fmax - 200);
	EnableArbMax(aiclk_arb_max_tdp, true);

	SetAiclkArbMax(aiclk_arb_max_thm, fmax - 150);
	EnableArbMax(aiclk_arb_max_thm, true);

	expected_max = fmax - 200;

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(expected_max, get_aiclk_effective_arb_max(&effective_max_arb));
	zexpect_equal(aiclk_arb_max_tdp, effective_max_arb,
		      "Expected TDP arbiter (200 MHz reduction) to be effective max");
	zassert_equal(targ_freq, expected_max,
		      "Target frequency (%d) should be equal to lowest max arbiter (%d)", targ_freq,
		      expected_max);
}

ZTEST(aiclk_ppm, test_arb_highest_min)
{
	uint32_t targ_freq;
	uint32_t expected_min;
	enum aiclk_arb_min effective_min_arb;

	/* Set multiple min arbiters to different values */
	SetAiclkArbMin(aiclk_arb_min_fmin, fmin + 100);
	EnableArbMin(aiclk_arb_min_fmin, true);

	SetAiclkArbMin(aiclk_arb_min_busy, fmin + 200);
	EnableArbMin(aiclk_arb_min_busy, true);

	expected_min = fmin + 200;

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zexpect_equal(expected_min, get_aiclk_effective_arb_min(&effective_min_arb));
	zexpect_equal(aiclk_arb_min_busy, effective_min_arb,
		      "Expected busy arbiter (200 MHz increase) to be effective min");
	zassert_equal(targ_freq, expected_min,
		      "Target frequency (%d) should be equal to highest min arbiter (%d)",
		      targ_freq, expected_min);
}

ZTEST(aiclk_ppm, test_max_arb_less_than_fmin)
{
	uint32_t targ_freq;

	/* Set fmax arbiter below fmin */
	SetAiclkArbMax(aiclk_arb_max_fmax, fmin - 100);
	EnableArbMax(aiclk_arb_max_fmax, true);

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
	SetAiclkArbMin(aiclk_arb_min_fmin, min_arb_value);
	EnableArbMin(aiclk_arb_min_fmin, true);

	SetAiclkArbMax(aiclk_arb_max_fmax, max_arb_value);
	EnableArbMax(aiclk_arb_max_fmax, true);

	CalculateTargAiclk();
	targ_freq = GetAiclkTarg();

	zassert_equal(targ_freq, max_arb_value,
		      "Target frequency (%d) should be equal to max arbiter value (%d) when min "
		      "arbiter is above max arbiter",
		      targ_freq, fmin);
}

ZTEST(aiclk_ppm, test_enabled_arb_min_bitmask)
{
	uint32_t bitmask;

	/* Initially all arbiters should be disabled (reset_arb) */
	bitmask = get_enabled_arb_min_bitmask();
	zassert_equal(bitmask, 0, "Bitmask should be 0 when all min arbiters are disabled");

	/* Enable aiclk_arb_min_fmin (bit 0) */
	EnableArbMin(aiclk_arb_min_fmin, true);
	bitmask = get_enabled_arb_min_bitmask();
	zassert_equal(bitmask, (1 << aiclk_arb_min_fmin),
		      "Bitmask should have bit %d set when aiclk_arb_min_fmin is enabled",
		      aiclk_arb_min_fmin);

	/* Enable aiclk_arb_min_busy (bit 1) as well */
	EnableArbMin(aiclk_arb_min_busy, true);
	bitmask = get_enabled_arb_min_bitmask();
	zassert_equal(bitmask, (1 << aiclk_arb_min_fmin) | (1 << aiclk_arb_min_busy),
		      "Bitmask should have bits %d and %d set when both arbiters are enabled",
		      aiclk_arb_min_fmin, aiclk_arb_min_busy);

	/* Disable aiclk_arb_min_fmin, only aiclk_arb_min_busy should be set */
	EnableArbMin(aiclk_arb_min_fmin, false);
	bitmask = get_enabled_arb_min_bitmask();
	zassert_equal(bitmask, (1 << aiclk_arb_min_busy),
		      "Bitmask should have only bit %d set when only aiclk_arb_min_busy is enabled",
		      aiclk_arb_min_busy);

	/* Enable all min arbiters */
	for (int i = 0; i < aiclk_arb_min_count; i++) {
		EnableArbMin(i, true);
	}
	bitmask = get_enabled_arb_min_bitmask();
	uint32_t expected_all = (1 << aiclk_arb_min_count) - 1;

	zassert_equal(
		bitmask, expected_all,
		"Bitmask (0x%x) should have all %d bits set (0x%x) when all arbiters are enabled",
		bitmask, aiclk_arb_min_count, expected_all);
}

ZTEST(aiclk_ppm, test_enabled_arb_max_bitmask)
{
	uint32_t bitmask;

	/* Initially all arbiters should be disabled (reset_arb) */
	bitmask = get_enabled_arb_max_bitmask();
	zassert_equal(bitmask, 0, "Bitmask should be 0 when all max arbiters are disabled");

	/* Enable aiclk_arb_max_fmax (bit 0) */
	EnableArbMax(aiclk_arb_max_fmax, true);
	bitmask = get_enabled_arb_max_bitmask();
	zassert_equal(bitmask, (1 << aiclk_arb_max_fmax),
		      "Bitmask should have bit %d set when aiclk_arb_max_fmax is enabled",
		      aiclk_arb_max_fmax);

	/* Enable aiclk_arb_max_tdp and aiclk_arb_max_thm as well */
	EnableArbMax(aiclk_arb_max_tdp, true);
	EnableArbMax(aiclk_arb_max_thm, true);
	bitmask = get_enabled_arb_max_bitmask();
	uint32_t expected =
		(1 << aiclk_arb_max_fmax) | (1 << aiclk_arb_max_tdp) | (1 << aiclk_arb_max_thm);
	zassert_equal(bitmask, expected,
		      "Bitmask (0x%x) should have bits %d, %d, and %d set (0x%x)", bitmask,
		      aiclk_arb_max_fmax, aiclk_arb_max_tdp, aiclk_arb_max_thm, expected);

	/* Disable aiclk_arb_max_tdp */
	EnableArbMax(aiclk_arb_max_tdp, false);
	bitmask = get_enabled_arb_max_bitmask();
	expected = (1 << aiclk_arb_max_fmax) | (1 << aiclk_arb_max_thm);
	zassert_equal(
		bitmask, expected,
		"Bitmask (0x%x) should have only bits %d and %d set (0x%x) after disabling TDP",
		bitmask, aiclk_arb_max_fmax, aiclk_arb_max_thm, expected);

	/* Enable all max arbiters */
	for (int i = 0; i < aiclk_arb_max_count; i++) {
		EnableArbMax(i, true);
	}
	bitmask = get_enabled_arb_max_bitmask();
	uint32_t expected_all = (1 << aiclk_arb_max_count) - 1;

	zassert_equal(
		bitmask, expected_all,
		"Bitmask (0x%x) should have all %d bits set (0x%x) when all arbiters are enabled",
		bitmask, aiclk_arb_max_count, expected_all);
}

ZTEST(aiclk_ppm, test_arb_bitmask_independent)
{
	uint32_t min_bitmask, max_bitmask;

	/* Enable some min and max arbiters independently and verify they don't interfere */
	EnableArbMin(aiclk_arb_min_fmin, true);
	EnableArbMax(aiclk_arb_max_tdp, true);
	EnableArbMax(aiclk_arb_max_thm, true);

	min_bitmask = get_enabled_arb_min_bitmask();
	max_bitmask = get_enabled_arb_max_bitmask();

	zassert_equal(min_bitmask, (1 << aiclk_arb_min_fmin),
		      "Min bitmask should only reflect min arbiters");
	zassert_equal(max_bitmask, (1 << aiclk_arb_max_tdp) | (1 << aiclk_arb_max_thm),
		      "Max bitmask should only reflect max arbiters");
}

ZTEST_SUITE(aiclk_ppm, NULL, aiclk_ppm_setup, reset_arb, NULL, reinit_arb);
