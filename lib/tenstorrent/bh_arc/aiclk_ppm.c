/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aiclk_ppm.h"
#include "dvfs.h"
#include "voltage.h"
#include "vf_curve.h"

#include <stdlib.h>

#include <tenstorrent/bh_power.h>
#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/sys_init_defines.h>
#include <zephyr/init.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/tracing/tracing.h>

static const struct device *const pll_dev_0 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pll0));

/* Bounds checks for FMAX and FMIN (in MHz) */
#define AICLK_FMAX_MAX 1400.0F
#define AICLK_FMAX_MIN 800.0F
#define AICLK_FMIN_MAX 800.0F
#define AICLK_FMIN_MIN 200.0F

/* aiclk control mode */
typedef enum {
	CLOCK_MODE_UNCONTROLLED = 1,
	CLOCK_MODE_PPM_FORCED = 2,
	CLOCK_MODE_PPM_UNFORCED = 3
} ClockControlMode;

typedef struct {
	bool enabled;
	float value;
} AiclkArb;

typedef struct {
	uint32_t curr_freq;   /* in MHz */
	uint32_t targ_freq;   /* in MHz */
	uint32_t boot_freq;   /* in MHz */
	uint32_t fmax;        /* in MHz */
	uint32_t fmin;        /* in MHz */
	uint32_t forced_freq; /* in MHz, a value of zero means disabled. */
	uint32_t sweep_en;    /* a value of one means enabled, otherwise disabled. */
	uint32_t sweep_low;   /* in MHz */
	uint32_t sweep_high;  /* in MHz */
	union aiclk_targ_freq_info lim_arb_info; /*information on the limiting arbiter */
	AiclkArb arbiter_max[aiclk_arb_max_count];
	AiclkArb arbiter_min[aiclk_arb_min_count];
} AiclkPPM;

static AiclkPPM aiclk_ppm = {
	.fmax = AICLK_FMAX_MAX,
	.fmin = AICLK_FMIN_MIN,
};

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

static bool last_msg_busy;

void SetAiclkArbMax(enum aiclk_arb_max arb_max, float freq)
{
	aiclk_ppm.arbiter_max[arb_max].value = CLAMP(freq, aiclk_ppm.fmin, aiclk_ppm.fmax);
}

void SetAiclkArbMin(enum aiclk_arb_min arb_min, float freq)
{
	aiclk_ppm.arbiter_min[arb_min].value = CLAMP(freq, aiclk_ppm.fmin, aiclk_ppm.fmax);
}

void EnableArbMax(enum aiclk_arb_max arb_max, bool enable)
{
	aiclk_ppm.arbiter_max[arb_max].enabled = enable;
}

void EnableArbMin(enum aiclk_arb_min arb_min, bool enable)
{
	aiclk_ppm.arbiter_min[arb_min].enabled = enable;
}

void CalculateTargAiclk(void)
{
	/* Calculate the target AICLK frequency */
	/* Start by calculating the highest arbiter_min */
	/* Then limit to the lowest arbiter_max */
	/* Finally make sure that the target frequency is at least Fmin */

	enum aiclk_arb_min min_arb = aiclk_arb_min_count;
	enum aiclk_arb_max max_arb = aiclk_arb_max_count;

	aiclk_ppm.targ_freq = get_aiclk_effective_arb_min(&min_arb);

	union aiclk_targ_freq_info info = {.reason = limit_reason_min_arb, .arbiter = min_arb};

	uint32_t max_arb_freq = get_aiclk_effective_arb_max(&max_arb);

	if (aiclk_ppm.targ_freq > max_arb_freq) {
		aiclk_ppm.targ_freq = max_arb_freq;
		info.reason = limit_reason_max_arb;
		info.arbiter = max_arb;
	}

	/* Make sure target is not below Fmin */
	/* (it will not be above Fmax, since we calculated the max limits last) */
	if (aiclk_ppm.targ_freq < aiclk_ppm.fmin) {
		aiclk_ppm.targ_freq = aiclk_ppm.fmin;
		info.reason = limit_reason_fmin;
		info.arbiter = 0U;
	}

	/* Apply random frequency if sweep is enabled */
	if (aiclk_ppm.sweep_en == 1) {
		aiclk_ppm.targ_freq = rand() % (aiclk_ppm.sweep_high - aiclk_ppm.sweep_low + 1) +
				      aiclk_ppm.sweep_low;

		info.reason = limit_reason_sweep;
		info.arbiter = 0U;
	}

	/* Apply forced frequency at the end, regardless of any limits */
	if (aiclk_ppm.forced_freq != 0) {
		aiclk_ppm.targ_freq = aiclk_ppm.forced_freq;
		info.reason = limit_reason_forced;
		info.arbiter = 0U;
	}

	aiclk_ppm.lim_arb_info = info;
	sys_trace_named_event("targ_freq_update", aiclk_ppm.targ_freq,
			      aiclk_ppm.lim_arb_info.u32_all);
}

void DecreaseAiclk(void)
{
	if (aiclk_ppm.targ_freq < aiclk_ppm.curr_freq) {
		clock_control_set_rate(pll_dev_0,
				       (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_AICLK,
				       (clock_control_subsys_rate_t)aiclk_ppm.targ_freq);
		aiclk_ppm.curr_freq = aiclk_ppm.targ_freq;
		sys_trace_named_event("aiclk_update", aiclk_ppm.curr_freq, aiclk_ppm.targ_freq);
	}
}

void IncreaseAiclk(void)
{
	if (aiclk_ppm.targ_freq > aiclk_ppm.curr_freq) {
		clock_control_set_rate(pll_dev_0,
				       (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_AICLK,
				       (clock_control_subsys_rate_t)aiclk_ppm.targ_freq);
		aiclk_ppm.curr_freq = aiclk_ppm.targ_freq;
		sys_trace_named_event("aiclk_update", aiclk_ppm.curr_freq, aiclk_ppm.targ_freq);
	}
}

float GetThrottlerArbMax(enum aiclk_arb_max arb_max)
{
	return aiclk_ppm.arbiter_max[arb_max].value;
}

/* TODO: Write a Zephyr unit test for this function */
uint32_t GetMaxAiclkForVoltage(uint32_t voltage)
{
	/* Assume monotonically increasing relationship between frequency and voltage */
	/* and conduct binary search. */
	/* Note this function doesn't work if you would need lower than fmin to achieve the voltage
	 */

	/* starting high_freq at fmax + 1 solves the case where the Max AICLK is fmax */
	uint32_t high_freq = aiclk_ppm.fmax + 1;
	uint32_t low_freq = aiclk_ppm.fmin;

	while (low_freq < high_freq) {
		uint32_t mid_freq = (low_freq + high_freq) / 2;

		if (VFCurve(mid_freq) > voltage) {
			high_freq = mid_freq;
		} else {
			low_freq = mid_freq + 1;
		}
	}

	return low_freq - 1;
}

void InitArbMaxVoltage(void)
{
	/* ArbMaxVoltage is statically set to the frequency of the maximum voltage */
	SetAiclkArbMax(aiclk_arb_max_voltage, GetMaxAiclkForVoltage(voltage_arbiter.vdd_max));
}

static int InitAiclkPPM(void)
{
	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		return 0;
	}

	/* Initialize some AICLK tracking variables */
	clock_control_get_rate(pll_dev_0, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_AICLK,
			       &aiclk_ppm.boot_freq);

	aiclk_ppm.curr_freq = aiclk_ppm.boot_freq;
	aiclk_ppm.targ_freq = aiclk_ppm.curr_freq;

	if (IS_ENABLED(CONFIG_ARC)) {
		aiclk_ppm.fmax =
			CLAMP(tt_bh_fwtable_get_fw_table(fwtable_dev)->chip_limits.asic_fmax,
			      AICLK_FMAX_MIN, AICLK_FMAX_MAX);
		aiclk_ppm.fmin =
			CLAMP(tt_bh_fwtable_get_fw_table(fwtable_dev)->chip_limits.asic_fmin,
			      AICLK_FMIN_MIN, AICLK_FMIN_MAX);
	}

	/* disable forcing of AICLK */
	aiclk_ppm.forced_freq = 0;

	/* disable AICLK sweep */
	aiclk_ppm.sweep_en = 0;

	for (int i = 0; i < aiclk_arb_max_count; i++) {
		aiclk_ppm.arbiter_max[i].value = aiclk_ppm.fmax;
		aiclk_ppm.arbiter_max[i].enabled = true;
	}

	for (int i = 0; i < aiclk_arb_min_count; i++) {
		aiclk_ppm.arbiter_min[i].value = aiclk_ppm.fmin;
		aiclk_ppm.arbiter_min[i].enabled = true;
	}

	return 0;
}
SYS_INIT_APP(InitAiclkPPM);

uint8_t ForceAiclk(uint32_t freq)
{
	if ((freq > AICLK_FMAX_MAX || freq < AICLK_FMIN_MIN) && (freq != 0)) {
		return 1;
	}

	if (dvfs_enabled) {
		aiclk_ppm.forced_freq = freq;
		DVFSChange();
	} else {
		/* restore to boot frequency */
		if (freq == 0) {
			freq = aiclk_ppm.boot_freq;
		}

		clock_control_set_rate(pll_dev_0,
				       (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_AICLK,
				       (clock_control_subsys_rate_t)freq);
	}
	return 0;
}

uint32_t GetAiclkTarg(void)
{
	return aiclk_ppm.targ_freq;
}

uint32_t GetAiclkFmin(void)
{
	return aiclk_ppm.fmin;
}

uint32_t GetAiclkFmax(void)
{
	return aiclk_ppm.fmax;
}

void aiclk_update_busy(void)
{
	if (last_msg_busy || bh_get_aiclk_busy()) {
		SetAiclkArbMin(aiclk_arb_min_busy, aiclk_ppm.fmax);
	} else {
		SetAiclkArbMin(aiclk_arb_min_busy, aiclk_ppm.fmin);
	}
}

uint32_t get_aiclk_effective_arb_min(enum aiclk_arb_min *effective_min_arb)
{
	/* Calculate the highest enabled arbiter_min */
	uint32_t effective_min = aiclk_ppm.fmin;

	for (enum aiclk_arb_min i = 0; i < aiclk_arb_min_count; i++) {
		if (aiclk_ppm.arbiter_min[i].enabled) {
			effective_min = MAX(effective_min, aiclk_ppm.arbiter_min[i].value);

			if (effective_min == aiclk_ppm.arbiter_min[i].value) {
				*effective_min_arb = i;
			}
		}
	}

	return effective_min;
}

uint32_t get_aiclk_effective_arb_max(enum aiclk_arb_max *effective_max_arb)
{
	/* Calculate the lowest enabled arbiter_max */
	uint32_t effective_max = aiclk_ppm.fmax;

	for (enum aiclk_arb_max i = 0; i < aiclk_arb_max_count; i++) {
		if (aiclk_ppm.arbiter_max[i].enabled) {
			effective_max = MIN(effective_max, aiclk_ppm.arbiter_max[i].value);

			if (effective_max == aiclk_ppm.arbiter_max[i].value) {
				*effective_max_arb = i;
			}
		}
	}

	return effective_max;
}

uint32_t get_enabled_arb_min_bitmask(void)
{
	/* Return a bitmask of enabled min arbiters */
	uint32_t bitmask = 0;

	for (enum aiclk_arb_min i = 0; i < aiclk_arb_min_count; i++) {
		if (aiclk_ppm.arbiter_min[i].enabled) {
			bitmask |= (1 << i);
		}
	}

	return bitmask;
}

uint32_t get_enabled_arb_max_bitmask(void)
{
	/* Return a bitmask of enabled max arbiters */
	uint32_t bitmask = 0;

	for (enum aiclk_arb_max i = 0; i < aiclk_arb_max_count; i++) {
		if (aiclk_ppm.arbiter_max[i].enabled) {
			bitmask |= (1 << i);
		}
	}

	return bitmask;
}

/** @brief Handles the request to set AICLK busy or idle
 * @param[in] request The request, of type @ref aiclk_set_speed_rqst_t, with command code
 *	@ref MSG_TYPE_AICLK_GO_BUSY to go busy, or @ref MSG_TYPE_AICLK_GO_LONG_IDLE to go idle.
 * @param[out] response The response to the host
 * @return 0 for success
 */
static uint8_t aiclk_busy_handler(const union request *request, struct response *response)
{
	last_msg_busy = (request->aiclk_set_speed.command_code == TT_SMC_MSG_AICLK_GO_BUSY);
	aiclk_update_busy();
	return 0;
}

static uint8_t ForceAiclkHandler(const union request *request, struct response *response)
{
	uint32_t forced_freq = request->data[1];

	return ForceAiclk(forced_freq);
}

/* This message returns aiclk and aiclk control mode */
static uint8_t get_aiclk_handler(const union request *request, struct response *response)
{
	clock_control_get_rate(pll_dev_0, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_AICLK,
			       &(response->data[1]));

	if (!dvfs_enabled) {
		response->data[2] = CLOCK_MODE_UNCONTROLLED;
	} else if (aiclk_ppm.forced_freq != 0) {
		response->data[2] = CLOCK_MODE_PPM_FORCED;
	} else {
		response->data[2] = CLOCK_MODE_PPM_UNFORCED;
	}

	return 0;
}

static uint8_t SweepAiclkHandler(const union request *request, struct response *response)
{
	if (request->command_code == TT_SMC_MSG_AISWEEP_START) {
		if (request->data[1] == 0 || request->data[2] == 0) {
			return 1;
		}
		aiclk_ppm.sweep_low = MAX(request->data[1], aiclk_ppm.fmin);
		aiclk_ppm.sweep_high = MIN(request->data[2], aiclk_ppm.fmax);
		aiclk_ppm.sweep_en = 1;
	} else {
		aiclk_ppm.sweep_en = 0;
	}
	return 0;
}

union aiclk_targ_freq_info get_targ_aiclk_info(void)
{
	return aiclk_ppm.lim_arb_info;
}

REGISTER_MESSAGE(TT_SMC_MSG_AICLK_GO_BUSY, aiclk_busy_handler);
REGISTER_MESSAGE(TT_SMC_MSG_AICLK_GO_LONG_IDLE, aiclk_busy_handler);
REGISTER_MESSAGE(TT_SMC_MSG_FORCE_AICLK, ForceAiclkHandler);
REGISTER_MESSAGE(TT_SMC_MSG_GET_AICLK, get_aiclk_handler);
REGISTER_MESSAGE(TT_SMC_MSG_AISWEEP_START, SweepAiclkHandler);
REGISTER_MESSAGE(TT_SMC_MSG_AISWEEP_STOP, SweepAiclkHandler);
