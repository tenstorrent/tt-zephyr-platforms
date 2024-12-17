/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/fan_ctrl.h>
#include <tenstorrent/tt_smbus.h>
#include <zephyr/device.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(tt_fan_ctrl, CONFIG_TT_FAN_CTRL_LOG_LEVEL);

static const struct device *const smbus1 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(smbus1));

int init_fan(void)
{
	uint8_t read_data;

	/* enable PWM manual mode, RPM to max */
	int ret = smbus_byte_data_write(smbus1, FAN_CTRL_ADDR, FAN1_CONFIG_1, 0x83);

	if (ret != 0) {
		return ret;
	}
	/* select high PWM frequency output range */
	ret = smbus_byte_data_write(smbus1, FAN_CTRL_ADDR, GLOBAL_CONFIG, 0x38);
	if (ret != 0) {
		return ret;
	}
	/* disable pulse stretching, deassert THERM, set PWM frequency to high */
	ret = smbus_byte_data_write(smbus1, FAN_CTRL_ADDR, FAN1_CONFIG_3, 0x23);
	if (ret != 0) {
		return ret;
	}

	smbus_byte_data_read(smbus1, FAN_CTRL_ADDR, FAN1_CONFIG_1, &read_data);
	LOG_DBG("FAN1_CONFIG_1: %x (Should be 0x83)", read_data);
	smbus_byte_data_read(smbus1, FAN_CTRL_ADDR, GLOBAL_CONFIG, &read_data);
	LOG_DBG("GLOBAL_CONFIG: %x (Should be 0x38)", read_data);
	smbus_byte_data_read(smbus1, FAN_CTRL_ADDR, FAN1_CONFIG_3, &read_data);
	LOG_DBG("FAN1_CONFIG_3: %x (Should be 0x23)", read_data);

	return ret;
}

int set_fan_speed(uint8_t fan_speed)
{
	uint8_t pwm_setting = fan_speed * 1.2; /* fan controller pwm has 120 time slots */
	int ret = smbus_byte_data_write(smbus1, FAN_CTRL_ADDR, FAN1_DUTY_CYCLE, pwm_setting);
	return ret;
}

uint8_t get_fan_duty_cycle(void)
{
	uint8_t pwm_setting;

	smbus_byte_data_read(smbus1, FAN_CTRL_ADDR, FAN1_DUTY_CYCLE, &pwm_setting);
	uint8_t fan_speed = pwm_setting / 1.2;

	LOG_DBG("FAN1_DUTY_CYCLE (converted to percentage): %d", fan_speed);

	return fan_speed;
}

uint16_t get_fan_rpm(void)
{
	uint8_t tach_count;
	uint16_t rpm_range = 16000; /* RPM range is maximum */

	smbus_byte_data_read(smbus1, FAN_CTRL_ADDR, TACH1, &tach_count);
	uint16_t rpm = rpm_range * 30 / tach_count;

	LOG_DBG("TACH1 count: %d", tach_count);
	LOG_DBG("Fan RPM: %d", rpm);

	return rpm;
}
