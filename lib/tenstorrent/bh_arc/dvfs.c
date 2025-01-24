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

	uint32_t aiclk_voltage = VFCurve(aiclk_ppm.targ_freq);

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
	InitAiclkPPM();
	InitThrottlers();
	dvfs_enabled = true;
}

void StartDVFSTimer(void)
{
	k_timer_start(&dvfs_timer, K_MSEC(1), K_MSEC(1));
}
