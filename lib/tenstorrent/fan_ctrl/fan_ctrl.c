/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/fan_ctrl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/mfd/max6639.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tt_fan_ctrl, CONFIG_TT_FAN_CTRL_LOG_LEVEL);

static const struct device *const max6639_pwm_dev =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(max6639_pwm));
static const struct device *const max6639_sensor_dev =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(max6639_sensor));

int set_fan_speed(uint8_t fan_speed)
{
	int ret = pwm_set_cycles(max6639_pwm_dev, 0, 100, fan_speed, 0);
	return ret;
}

uint8_t get_fan_duty_cycle(void)
{
	struct sensor_value data;

	sensor_sample_fetch_chan(max6639_sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE);
	sensor_channel_get(max6639_sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE, &data);

	LOG_DBG("FAN1_DUTY_CYCLE (converted to percentage): %d", data.val1);

	return data.val1;
}

uint16_t get_fan_rpm(void)
{
	struct sensor_value data;

	sensor_sample_fetch_chan(max6639_sensor_dev, MAX6639_CHAN_1_RPM);
	sensor_channel_get(max6639_sensor_dev, MAX6639_CHAN_1_RPM, &data);

	LOG_DBG("Fan RPM: %d", data.val1);

	return data.val1;
}
