/*
 * Copyright (c) 2024 Tenstorrent AI ULC
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
static int fan_ctrl_update_interval = 5000;

uint32_t fan_table_x1;
uint32_t fan_table_x2;
uint32_t fan_table_y1;
uint32_t fan_table_y2;

uint32_t fan_speed;

float asic_temp;
float alpha = 0.5; /* TODO: what value of alpha to use? */

static void update_fan_speed(void)
{
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	asic_temp = alpha * telemetry_internal_data.asic_temperature +
			(1 - alpha) * asic_temp;

	fan_speed = ((fan_table_y2 - fan_table_y1) /
		(float) (fan_table_x2 - fan_table_x1)) * asic_temp + fan_table_y1;
	if (fan_speed < fan_table_y1) {
		fan_speed = fan_table_y1;
	}
	if (fan_speed > fan_table_y2) {
		fan_speed = fan_table_y2;
	}

	/* Send fan speed to BMFW */
	UpdateFanSpeedRequest(fan_speed);
}

static uint8_t force_fan_speed(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	if (request->data[1] == 0xFFFFFFFF) { /* unforce */
		k_timer_start(&fan_ctrl_update_timer,
			      K_MSEC(fan_ctrl_update_interval),
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
	/* Get fan table */
	fan_table_x1 = get_fw_table()->fan_table.fan_table_point_x1;
	fan_table_x2 = get_fw_table()->fan_table.fan_table_point_x2;
	fan_table_y1 = get_fw_table()->fan_table.fan_table_point_y1;
	fan_table_y2 = get_fw_table()->fan_table.fan_table_point_y2;

	/* Don't let fan speed default to 0 if fan table uninitialized */
	if (fan_table_y1 == 0 && fan_table_y2 == 0) {
		fan_table_y1 = 100;
		fan_table_y2 = 100;
	}

	/* Get initial asic temp */
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(1, &telemetry_internal_data);
	asic_temp = telemetry_internal_data.asic_temperature;

	/* start a periodic timer that expires once every fan_ctrl_update_interval */
	k_timer_start(&fan_ctrl_update_timer, K_MSEC(fan_ctrl_update_interval),
		      K_MSEC(fan_ctrl_update_interval));
}

REGISTER_MESSAGE(MSG_TYPE_FORCE_FAN_SPEED, force_fan_speed);
