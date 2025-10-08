/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include "vf_curve.h"
#include "throttler.h"
#include "aiclk_ppm.h"
#include "voltage.h"

bool dvfs_enabled;

void DVFSChange(void)
{
	CalculateThrottlers();
	CalculateTargAiclk();

	uint32_t targ_freq = GetAiclkTarg();
	uint32_t aiclk_voltage = VFCurve(targ_freq);

	VoltageArbRequest(VoltageReqAiclk, aiclk_voltage);

	CalculateTargVoltage();

	DecreaseAiclk();
	VoltageChange();
	IncreaseAiclk();
}

static void dvfs_work_handler(struct k_work *work)
{
	DVFSChange();
}
static K_WORK_DEFINE(dvfs_worker, dvfs_work_handler);

static void dvfs_timer_handler(struct k_timer *timer)
{
	k_work_submit(&dvfs_worker);
}
static K_TIMER_DEFINE(dvfs_timer, dvfs_timer_handler, NULL);

void InitDVFS(void)
{
	InitVFCurve();
	InitVoltagePPM();
	InitArbMaxVoltage();
	InitThrottlers();
	dvfs_enabled = true;
}

#define DVFS_MSEC 1

void StartDVFSTimer(void)
{
	k_timer_start(&dvfs_timer, K_MSEC(DVFS_MSEC), K_MSEC(DVFS_MSEC));
}

#define DVFS_TICKS (CONFIG_SYS_CLOCK_TICKS_PER_SEC * DVFS_MSEC / MSEC_PER_SEC)

/* If DVFS is already scheduled "close enough" to the board power message, then don't try to adjust
 * it. There may be some jitter in the message arrival and we don't want to suddenly go from being
 * very close to very far away. 10% is arbitrary.
 */
#define DVFS_ADJUSTMENT_THRESHOLD (DVFS_TICKS * 10 / 100) /* 10% of DVFS interval */

/* DVFS's PID controllers assume they are run on a 1ms interval. Changing the interval implicitly
 * changes their behaviour. 1% should be small enough to not cause trouble.
 */
#define DVFS_ADJUSTMENT_STEP (DVFS_TICKS * 1 / 100) /* 1% of DVFS interval */

void AdjustDVFSTimer(void)
{
	/* We just received a board power update from the DMC. If DVFS is still more than 10% of
	 * its interval away, then reduce that time by 1%. Over enough cycles, this should bring
	 * the DMC->DVFS latency down.
	 */
	if (dvfs_enabled) {
		k_ticks_t dvfs_remaining = k_timer_remaining_ticks(&dvfs_timer);

		if (dvfs_remaining > DVFS_ADJUSTMENT_THRESHOLD) {
			k_timeout_t delay = K_TICKS(dvfs_remaining - DVFS_ADJUSTMENT_STEP);

			k_timer_start(&dvfs_timer, delay, K_MSEC(DVFS_MSEC));
		}
	}
}
