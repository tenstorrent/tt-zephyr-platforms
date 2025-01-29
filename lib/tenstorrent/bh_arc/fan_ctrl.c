/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fan_ctrl.h"

#include "cm2bm_msg.h"
#include "dw_apb_i2c.h"
#include "fw_table.h"
#include "gddr.h"
#include "reg.h"
#include "status_reg.h"
#include "telemetry_internal.h"
#include "telemetry.h"
#include "timer.h"

#include <zephyr/kernel.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/msg_type.h>

static struct k_timer fan_ctrl_update_timer;
static struct k_work fan_ctrl_update_worker;
static int fan_ctrl_update_interval = 1000;

uint32_t fan_speed; /* In PWM for now*/
float max_gddr_temp;
float max_asic_temp;
float alpha = CONFIG_TT_BH_ARC_FAN_CTRL_ALPHA / 100.0f;

static uint16_t read_max_gddr_temp(void)
{
	uint16_t max_temp = 0;
	gddr_telemetry_table_t telemetry;

	for (uint8_t gddr_inst = 0; gddr_inst < 8; gddr_inst++) {
		read_gddr_telemetry_table(gddr_inst, &telemetry);

		if (telemetry.dram_temperature_bottom > max_temp) {
			max_temp = telemetry.dram_temperature_bottom;
		}
		if (telemetry.dram_temperature_top > max_temp) {
			max_temp = telemetry.dram_temperature_top;
		}
	}

	return max_temp;
}

static uint32_t fan_curve(float max_asic_temp, float max_gddr_temp)
{
	/* P150 fan curve */
	/* uint32_t fan_rpm[10] = {1800, 1990, 2170, 2400, 2650, 2885, 3155, 3440, 4800, 5380}; */
	uint32_t fan_pwm[10] = {35, 40, 45, 50, 55, 60, 65, 70, 90, 100};
	float gddr_temps[9] = {46.0, 52.0, 59.0, 64.0, 68.0, 71.0, 74.0, 77.0, 80.0};
	float asic_temps[9] = {52.0, 56.0, 60.0, 65.0, 70.0, 74.0, 80.0, 85.0, 92.0};

	uint32_t fan_speed1 = fan_pwm[0];
	uint32_t fan_speed2 = fan_pwm[0];

	for (int i = 0; i < 9; i++) {
		if (max_asic_temp >= asic_temps[i]) {
			fan_speed1 = fan_pwm[i + 1];
		}
		if (max_gddr_temp >= gddr_temps[i]) {
			fan_speed2 = fan_pwm[i + 1];
		}
	}

	return (fan_speed1 > fan_speed2) ? fan_speed1 : fan_speed2;
}

static void update_fan_speed(void)
{
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	max_asic_temp =
		alpha * telemetry_internal_data.asic_temperature + (1 - alpha) * max_asic_temp;

	if (IS_ENABLED(CONFIG_TT_BH_ARC_FAN_CTRL_GDDR_TEMP)) {
		max_gddr_temp = alpha * read_max_gddr_temp() + (1 - alpha) * max_gddr_temp;
	} else {
		max_gddr_temp = 0;
	}

	fan_speed = fan_curve(max_asic_temp, max_gddr_temp);

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
