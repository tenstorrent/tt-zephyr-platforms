/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aiclk_ppm.h"

#include <zephyr/sys/util.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "pll.h"
#include "voltage.h"
#include "vf_curve.h"
#include "dvfs.h"
#include "fw_table.h"

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

AiclkPPM aiclk_ppm;

void SetAiclkArbMax(AiclkArbMax arb_max, float freq)
{
	aiclk_ppm.arbiter_max[arb_max].value = CLAMP(freq, aiclk_ppm.fmin, aiclk_ppm.fmax);
}

void SetAiclkArbMin(AiclkArbMin arb_min, float freq)
{
	aiclk_ppm.arbiter_min[arb_min].value = CLAMP(freq, aiclk_ppm.fmin, aiclk_ppm.fmax);
}

void EnableArbMax(AiclkArbMax arb_max, bool enable)
{
	aiclk_ppm.arbiter_max[arb_max].enabled = enable;
}

void EnableArbMin(AiclkArbMin arb_min, bool enable)
{
	aiclk_ppm.arbiter_min[arb_min].enabled = enable;
}

void CalculateTargAiclk(void)
{
	/* Calculate the target AICLK frequency */
	/* Start by calculating the highest arbiter_min */
	/* Then limit to the lowest arbiter_max */
	/* Finally make sure that the target frequency is at least Fmin */
	uint32_t targ_freq = aiclk_ppm.fmin;

	for (AiclkArbMin i = 0; i < kAiclkArbMinCount; i++) {
		if (aiclk_ppm.arbiter_min[i].enabled)
			targ_freq = MAX(targ_freq, aiclk_ppm.arbiter_min[i].value);
	}

	for (AiclkArbMax i = 0; i < kAiclkArbMaxCount; i++) {
		if (aiclk_ppm.arbiter_max[i].enabled)
			targ_freq = MIN(targ_freq, aiclk_ppm.arbiter_max[i].value);
	}

	/* Make sure target is not below Fmin */
	/* (it will not be above Fmax, since we calculated the max limits last) */
	aiclk_ppm.targ_freq = MAX(targ_freq, aiclk_ppm.fmin);

	/* Apply forced frequency at the end, regardless of any limits */
	if (aiclk_ppm.forced_freq != 0) {
		aiclk_ppm.targ_freq = aiclk_ppm.forced_freq;
	}
}

void DecreaseAiclk(void)
{
	if (aiclk_ppm.targ_freq < aiclk_ppm.curr_freq) {
		SetAICLK(aiclk_ppm.targ_freq);
		aiclk_ppm.curr_freq = aiclk_ppm.targ_freq;
	}
}

void IncreaseAiclk(void)
{
	if (aiclk_ppm.targ_freq > aiclk_ppm.curr_freq) {
		SetAICLK(aiclk_ppm.targ_freq);
		aiclk_ppm.curr_freq = aiclk_ppm.targ_freq;
	}
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
	SetAiclkArbMax(kAiclkArbMaxVoltage, GetMaxAiclkForVoltage(voltage_arbiter.vdd_max));
}

void InitAiclkPPM(void)
{
	aiclk_ppm.boot_freq = GetAICLK();
	aiclk_ppm.curr_freq = aiclk_ppm.boot_freq;
	aiclk_ppm.targ_freq = aiclk_ppm.curr_freq;

	aiclk_ppm.fmax =
		CLAMP(get_fw_table()->chip_limits.asic_fmax, AICLK_FMAX_MIN, AICLK_FMAX_MAX);
	aiclk_ppm.fmin =
		CLAMP(get_fw_table()->chip_limits.asic_fmin, AICLK_FMIN_MIN, AICLK_FMIN_MAX);

	/* disable forcing of AICLK */
	aiclk_ppm.forced_freq = 0;

	for (int i = 0; i < kAiclkArbMaxCount; i++) {
		aiclk_ppm.arbiter_max[i].value = aiclk_ppm.fmax;
		aiclk_ppm.arbiter_max[i].enabled = true;
	}

	for (int i = 0; i < kAiclkArbMinCount; i++) {
		aiclk_ppm.arbiter_min[i].value = aiclk_ppm.fmin;
		aiclk_ppm.arbiter_max[i].enabled = true;
	}
}

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

		SetAICLK(freq);
	}
	return 0;
}

static uint8_t AiclkBusyHandler(uint32_t msg_code, const struct request *request,
				struct response *response)
{
	if (msg_code == MSG_TYPE_AICLK_GO_BUSY) {
		SetAiclkArbMin(kAiclkArbMinBusy, aiclk_ppm.fmax);
	} else {
		SetAiclkArbMin(kAiclkArbMinBusy, aiclk_ppm.fmin);
	}
	return 0;
}

static uint8_t ForceAiclkHandler(uint32_t msg_code, const struct request *request,
				 struct response *response)
{
	uint32_t forced_freq = request->data[1];

	return ForceAiclk(forced_freq);
}

/* This message returns aiclk and aiclk control mode */
static uint8_t get_aiclk_handler(uint32_t msg_code, const struct request *request,
				 struct response *response)
{
	response->data[1] = GetAICLK();

	if (!dvfs_enabled) {
		response->data[2] = CLOCK_MODE_UNCONTROLLED;
	} else if (aiclk_ppm.forced_freq != 0) {
		response->data[2] = CLOCK_MODE_PPM_FORCED;
	} else {
		response->data[2] = CLOCK_MODE_PPM_UNFORCED;
	}

	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_AICLK_GO_BUSY, AiclkBusyHandler);
REGISTER_MESSAGE(MSG_TYPE_AICLK_GO_LONG_IDLE, AiclkBusyHandler);
REGISTER_MESSAGE(MSG_TYPE_FORCE_AICLK, ForceAiclkHandler);
REGISTER_MESSAGE(MSG_TYPE_GET_AICLK, get_aiclk_handler);
