/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fan_ctrl.h"

#include "cm2dm_msg.h"
#include "fw_table.h"
#include "gddr.h"
#include "telemetry_internal.h"
#include "telemetry.h"
#include "timer.h"
#include "harvesting.h"

#include <tenstorrent/msgqueue.h>
#include <tenstorrent/msg_type.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_ZTEST
#define STATIC
#else
#define STATIC static
#endif

LOG_MODULE_REGISTER(fan_ctrl, CONFIG_TT_APP_LOG_LEVEL);

static struct k_timer fan_ctrl_update_timer;
static struct k_work fan_ctrl_update_worker;
static int fan_ctrl_update_interval = 1000;

uint16_t fan_rpm; /* Fan RPM from tach */
uint32_t fan_speed; /* In PWM for now */
float max_gddr_temp;
float max_asic_temp;
float alpha = CONFIG_TT_BH_ARC_FAN_CTRL_ALPHA / 100.0f;

STATIC uint32_t fan_curve(float max_asic_temp, float max_gddr_temp)
{
	/* P150 fan curve: could be a part of device tree once added to the driver model */
	uint32_t fan_speed1;
	uint32_t fan_speed2;

	if (max_asic_temp < 49) {
		fan_speed1 = 35;
	} else if (max_asic_temp < 90) {
		fan_speed1 =
			(uint32_t)(0.03867f * (max_asic_temp - 49.0f) * (max_asic_temp - 49.0f)) +
			35;
	} else {
		fan_speed1 = 100;
	}

	if (max_gddr_temp < 43) {
		fan_speed2 = 35;
	} else if (max_gddr_temp < 82) {
		fan_speed2 =
			(uint32_t)(0.04274f * (max_gddr_temp - 43.0f) * (max_gddr_temp - 43.0f)) +
			35;
	} else {
		fan_speed2 = 100;
	}

	return MAX(fan_speed1, fan_speed2);
}

static void update_fan_speed(void)
{
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	max_asic_temp =
		alpha * telemetry_internal_data.asic_temperature + (1 - alpha) * max_asic_temp;

	if (IS_ENABLED(CONFIG_TT_BH_ARC_FAN_CTRL_GDDR_TEMP)) {
		max_gddr_temp = alpha * GetMaxGDDRTemp() + (1 - alpha) * max_gddr_temp;
	} else {
		max_gddr_temp = 0;
	}

	fan_speed = fan_curve(max_asic_temp, max_gddr_temp);

	UpdateFanSpeedRequest(fan_speed);
}

uint16_t GetFanRPM(void)
{
	return fan_rpm;
}

void SetFanRPM(uint16_t rpm)
{
	fan_rpm = rpm;
}

uint32_t GetFanSpeed(void)
{
	return fan_speed;
}

static void fan_ctrl_work_handler(struct k_work *work)
{
	/* do the processing that needs to be done periodically */
	update_fan_speed();
}
static K_WORK_DEFINE(fan_ctrl_update_worker, fan_ctrl_work_handler);

static void fan_ctrl_timer_handler(struct k_timer *timer)
{
	k_work_submit(&fan_ctrl_update_worker);
}
static K_TIMER_DEFINE(fan_ctrl_update_timer, fan_ctrl_timer_handler, NULL);

void init_fan_ctrl(void)
{
	/* Get initial asic temp */
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	max_asic_temp = telemetry_internal_data.asic_temperature;

	/* start a periodic timer that expires once every fan_ctrl_update_interval */
	k_timer_start(&fan_ctrl_update_timer, K_MSEC(fan_ctrl_update_interval),
		      K_MSEC(fan_ctrl_update_interval));
}

static uint8_t force_fan_speed(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	if (request->data[1] == 0xFFFFFFFF) { /* unforce */
		k_timer_start(&fan_ctrl_update_timer, K_MSEC(fan_ctrl_update_interval),
				K_MSEC(fan_ctrl_update_interval));
	} else { /* force */
		k_timer_stop(&fan_ctrl_update_timer);
		fan_speed = request->data[1];
		UpdateFanSpeedRequest(fan_speed);
	}
	return 0;
}
REGISTER_MESSAGE(MSG_TYPE_FORCE_FAN_SPEED, force_fan_speed);
