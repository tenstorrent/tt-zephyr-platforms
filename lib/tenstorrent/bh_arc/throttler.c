/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include "throttler.h"
#include "aiclk_ppm.h"
#include "cm2dm_msg.h"
#include "fw_table.h"
#include "telemetry_internal.h"
#include "telemetry.h"
#include "noc2axi.h"
#include "status_reg.h"
#include "reg.h"

static const bool doppler = true;

static uint32_t power_limit;

#define kThrottlerAiclkScaleFactor 500.0F
#define DEFAULT_BOARD_POWER_LIMIT  150

LOG_MODULE_REGISTER(throttler);
typedef enum {
	kThrottlerTDP,
	kThrottlerFastTDC,
	kThrottlerTDC,
	kThrottlerThm,
	kThrottlerBoardPower,
	kThrottlerGDDRThm,
	kThrottlerCount,
} ThrottlerId;

typedef struct {
	float min;
	float max;
} ThrottlerLimitRange;

/* This table is used to restrict the throttler limits to reasonable ranges. */
/* They are passed in from the FW table in SPI */
static const ThrottlerLimitRange throttler_limit_ranges[kThrottlerCount] = {
	[kThrottlerTDP] = {
			.min = 50,
			.max = 500,
		},
	[kThrottlerFastTDC] = {
			.min = 50,
			.max = 500,
		},
	[kThrottlerTDC] = {
			.min = 50,
			.max = 400,
		},
	[kThrottlerThm] = {
			.min = 50,
			.max = 100,
		},
	[kThrottlerBoardPower] = {
			.min = 50,
			.max = 600,
		},
	[kThrottlerGDDRThm] = {
		.min = 50,
		.max = 100,
	}};

typedef struct {
	float alpha_filter;
	float p_gain;
	float d_gain;
} ThrottlerParams;

typedef struct {
	const AiclkArbMax arb_max; /* The arbiter associated with this throttler */

	const ThrottlerParams params;
	float limit;
	float value;
	float error;
	float prev_error;
	float output;
} Throttler;

static Throttler throttler[kThrottlerCount] = {
	[kThrottlerTDP] = {

			.arb_max = kAiclkArbMaxTDP,
			.params = {

					.alpha_filter = 1.0,
					.p_gain = 0.2,
					.d_gain = 0,
				},
		},
	[kThrottlerFastTDC] = {

			.arb_max = kAiclkArbMaxFastTDC,
			.params = {

					.alpha_filter = 1.0,
					.p_gain = 0.5,
					.d_gain = 0,
				},
		},
	[kThrottlerTDC] = {

			.arb_max = kAiclkArbMaxTDC,
			.params = {

					.alpha_filter = 0.1,
					.p_gain = 0.2,
					.d_gain = 0,
				},
		},
	[kThrottlerThm] = {

			.arb_max = kAiclkArbMaxThm,
			.params = {

					.alpha_filter = 1.0,
					.p_gain = 0.2,
					.d_gain = 0,
				},
		},
	[kThrottlerBoardPower] = {
			.arb_max = kAiclkArbMaxBoardPower,
			.params = {

					.alpha_filter = 1.0,
					.p_gain = 0.1,
					.d_gain = 0.1,
			}
	},
	[kThrottlerGDDRThm] = {
			.arb_max = kAiclkArbMaxGDDRThm,
			.params = {

					.alpha_filter = 1.0,
					.p_gain = 0.2,
					.d_gain = 0,
				},
	}
};

static void SetThrottlerLimit(ThrottlerId id, float limit)
{
	float clamped_limit =
		CLAMP(limit, throttler_limit_ranges[id].min, throttler_limit_ranges[id].max);

	LOG_INF("Throttler %d limit set to %d\n", id, (uint32_t)clamped_limit);
	throttler[id].limit = clamped_limit;
}

void InitThrottlers(void)
{
	SetThrottlerLimit(kThrottlerTDP, get_fw_table()->chip_limits.tdp_limit);
	SetThrottlerLimit(kThrottlerFastTDC, get_fw_table()->chip_limits.tdc_fast_limit);
	SetThrottlerLimit(kThrottlerTDC, get_fw_table()->chip_limits.tdc_limit);
	SetThrottlerLimit(kThrottlerThm, get_fw_table()->chip_limits.thm_limit);
	SetThrottlerLimit(kThrottlerBoardPower, DEFAULT_BOARD_POWER_LIMIT);
	SetThrottlerLimit(kThrottlerGDDRThm, get_fw_table()->chip_limits.gddr_thm_limit);
}

static void UpdateThrottler(ThrottlerId id, float value)
{
	Throttler *t = &throttler[id];

	t->value = t->params.alpha_filter * value + (1 - t->params.alpha_filter) * t->value;
	t->error = (t->limit - t->value) / t->limit;
	t->output = t->params.p_gain * t->error + t->params.d_gain * (t->error - t->prev_error);
	t->prev_error = t->error;
}

static void UpdateThrottlerArb(ThrottlerId id)
{
	Throttler *t = &throttler[id];

	float arb_val = aiclk_ppm.arbiter_max[t->arb_max].value;

	arb_val += t->output * kThrottlerAiclkScaleFactor;

	SetAiclkArbMax(t->arb_max, arb_val);
}

static uint16_t board_power_history[1000] = {0,};
static uint16_t *board_power_history_cursor = board_power_history;
static uint32_t board_power_sum = 0;
static bool board_power_throttling = false;

#define ADVANCE_CIRCULAR_POINTER(pointer, array)		\
	do {							\
		if (++(pointer) == (array) + ARRAY_SIZE(array))	\
			(pointer) = (array);			\
	} while (false)

static uint16_t UpdateMovingAveragePower(uint16_t current_power)
{
	board_power_sum += current_power - *board_power_history_cursor;
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(62), board_power_sum);
	*board_power_history_cursor = current_power;

	ADVANCE_CIRCULAR_POINTER(board_power_history_cursor, board_power_history);

	return board_power_sum / ARRAY_SIZE(board_power_history);
}

/* must only be called when throttle state changes */
static void SendKernelThrottlingMessage(bool throttle)
{
	const uint8_t kNocRing = 0;
	const uint8_t kNocTlb = 1; /* should reserve and pre-program a TLB for this */
	const uint32_t kKernelThrottleAddress = 0x10;

	/* The LLK uses fast = even, slow = odd, but for debug purposes, they'd like to
	 * know how many times throttling has happened. Just in case CMFW somehow gets
	 * out of sync internally, double-check the parity. */
	static uint32_t throttle_counter = 0;
	throttle_counter++;
	if ((throttle_counter & 1) != throttle)
		throttle_counter++;

	/* should reserve a TLB for this */
	NOC2AXITensixBroadcastTlbSetup(kNocRing, kNocTlb, kKernelThrottleAddress, kNoc2AxiOrderingStrict);
	NOC2AXIWrite32(kNocRing, kNocTlb, kKernelThrottleAddress, throttle_counter);
}

void CalculateThrottlers(void)
{
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);

	uint16_t moving_average_power = UpdateMovingAveragePower(GetInputPower());

	/* need to ensure all proper arbiters are enabled/disabled */
	if (doppler && power_limit > 0) {
		bool new_board_power_throttling = (moving_average_power >= power_limit);

		if (new_board_power_throttling != board_power_throttling) {
			SendKernelThrottlingMessage(new_board_power_throttling);

			SetAiclkArbMax(kAiclkArbMaxAverageBoardPower,
				       new_board_power_throttling ? 800 : 1300);

			board_power_throttling = new_board_power_throttling;
		}
	} else {
		UpdateThrottler(kThrottlerTDP, telemetry_internal_data.vcore_power);
		UpdateThrottler(kThrottlerFastTDC, telemetry_internal_data.vcore_current);
		UpdateThrottler(kThrottlerTDC, telemetry_internal_data.vcore_current);
		UpdateThrottler(kThrottlerBoardPower, GetInputPower());
	}

	UpdateThrottler(kThrottlerThm, telemetry_internal_data.asic_temperature);
	UpdateThrottler(kThrottlerGDDRThm, GetMaxGDDRTemp());

	for (ThrottlerId i = 0; i < kThrottlerCount; i++) {
		UpdateThrottlerArb(i);
	}
}

int32_t Dm2CmSetBoardPowerLimit(const uint8_t *data, uint8_t size)
{
	if (size != 2) {
		return -1;
	}

	power_limit = sys_get_le16(data);

	LOG_INF("Cable Power Limit: %u\n", power_limit);
	power_limit = MIN(power_limit, get_fw_table()->chip_limits.board_power_limit);

	SetThrottlerLimit(kThrottlerBoardPower, power_limit);
	UpdateTelemetryBoardPowerLimit(power_limit);

	return 0;
}
