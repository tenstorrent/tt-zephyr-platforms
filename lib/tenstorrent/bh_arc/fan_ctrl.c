/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fan_ctrl.h"

#include "cm2bm_msg.h"
#include "dw_apb_i2c.h"
#include "fw_table.h"
#include "reg.h"
#include "telemetry_internal.h"
#include "telemetry.h"
#include "timer.h"

#include <zephyr/kernel.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/msg_type.h>

static struct k_timer fan_ctrl_update_timer;
static struct k_work fan_ctrl_update_worker;
static int fan_ctrl_update_interval = 1000;

uint32_t fan_speed; // in PWM for now
uint16_t max_gddr_temp;
float max_asic_temp;
float alpha = 0.5;

static uint32_t fan_curve(float max_asic_temp, uint16_t max_gddr_temp)
{
	// P150 fan curve
	// uint32_t fan_rpm[10] = {1800, 1990, 2170, 2400, 2650, 2885, 3155, 3440, 4800, 5380};
	uint32_t fan_pwm[10] = {35, 40, 45, 50, 55, 60, 65, 70, 90, 100};
	uint16_t gddr_temps[9] = {46, 52, 59, 64, 68, 71, 74, 77, 80};
	float asic_temps[9] = {52.0, 56.0, 60.0, 65.0, 70.0, 74.0, 80.0, 85.0, 92.0};

	uint32_t fan_speed1 = fan_pwm[9];
	uint32_t fan_speed2 = fan_pwm[9];

	for (int i = 0; i < 9; i++) {
		if (max_asic_temp < asic_temps[i]) {
			fan_speed1 = fan_pwm[i];
		}
		if (max_gddr_temp < gddr_temps[i]) {
			fan_speed2 = fan_pwm[i];
		}
	}

	return (fan_speed1 > fan_speed2) ? fan_speed1 : fan_speed2;
}

static void update_fan_speed()
{
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	max_asic_temp = alpha * telemetry_internal_data.asic_temperature + (1 - alpha) * max_asic_temp;
	max_gddr_temp = 0;

	fan_speed = fan_curve(max_asic_temp, max_gddr_temp);

	// Send fan speed to BMFW
	UpdateFanSpeedRequest(fan_speed);
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

REGISTER_MESSAGE(MSG_TYPE_FORCE_FAN_SPEED, force_fan_speed);
