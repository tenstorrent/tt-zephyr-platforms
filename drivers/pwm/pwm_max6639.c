/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max6639_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/max6639.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(max6639_pwm, LOG_LEVEL_DBG);

struct max6639_pwm_config {
	struct i2c_dt_spec i2c;
};

static int max6639_pwm_set_cycles(const struct device *dev, uint32_t channel, uint32_t period_count,
				  uint32_t pulse_count, pwm_flags_t flags)
{
	int r;
	uint8_t duty_cycle_reg_addr;

	LOG_DBG("%s(%p, %u, %u, %u, %u)", __func__, dev, channel, period_count, pulse_count, flags);

	switch (channel) {
	case 0:
		duty_cycle_reg_addr = MAX6639_REG_CHANNEL_1_DUTY_CYCLE;
		break;
	case 1:
		duty_cycle_reg_addr = MAX6639_REG_CHANNEL_2_DUTY_CYCLE;
		break;
	default:
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	const struct max6639_pwm_config *config = dev->config;
	uint8_t fan_speed = (uint32_t)pulse_count * MAX6639_PWM_PERIOD / period_count;

	LOG_DBG("%s(%p, %u, %u, %u, %u) -> fan_speed=%u", "i2c_reg_write_byte_dt", config->i2c.bus,
		channel, period_count, pulse_count, flags, fan_speed);

	r = i2c_reg_write_byte_dt(&config->i2c, duty_cycle_reg_addr, fan_speed);
	if (r < 0) {
		LOG_ERR("%s() failed: %d", "i2c_reg_write_byte_dt", r);
	}

	return r;
}

static int max6639_pwm_get_cycles_per_sec(const struct device *dev, uint32_t channel,
					  uint64_t *cycles)
{
	const struct max6639_pwm_config *config = dev->config;
	int result;

	uint8_t config_3_reg_addr;

	switch (channel) {
	case 0:
		config_3_reg_addr = MAX6639_REG_CHANNEL_1_CONFIG_3;
		break;
	case 1:
		config_3_reg_addr = MAX6639_REG_CHANNEL_2_CONFIG_3;
		break;
	default:
		return -EINVAL;
	}

	uint8_t global_config;

	result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_GLOBAL_CONFIG, &global_config);
	if (result != 0) {
		return result;
	}

	uint8_t config_3;

	result = i2c_reg_read_byte_dt(&config->i2c, config_3_reg_addr, &config_3);
	if (result != 0) {
		return result;
	}

	static const uint16_t frequency_table[] = {
		MAX6639_HIGH_FREQ_00_FREQ,
		MAX6639_HIGH_FREQ_01_FREQ,
		MAX6639_HIGH_FREQ_10_FREQ,
		MAX6639_HIGH_FREQ_11_FREQ,
	};

	uint32_t frequency = frequency_table[config_3 & MAX6639_CONFIG_3_PWM_FREQUENCY_MASK];

	if (!IS_BIT_SET(global_config, MAX6639_REG_GLOBAL_CONFIG_PWM_FREQUENCY_SHIFT)) {
		frequency /= MAX6639_HIGH_LOW_FREQ_RATIO;
	}

	*cycles = frequency;

	return 0;
}

static int max6639_pwm_init(const struct device *dev)
{
	const struct max6639_pwm_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	return 0;
}

static DEVICE_API(pwm, max6639_pwm_api) = {
	.set_cycles = max6639_pwm_set_cycles,
	.get_cycles_per_sec = max6639_pwm_get_cycles_per_sec,
};

#define MAX6639_PWM_INIT(inst)                                                                     \
	static const struct max6639_pwm_config max6639_pwm_##inst##_config = {                     \
		.i2c = I2C_DT_SPEC_GET(DT_INST_PARENT(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, max6639_pwm_init, NULL, NULL, &max6639_pwm_##inst##_config,    \
			      POST_KERNEL, CONFIG_PWM_MAX6639_INIT_PRIORITY, &max6639_pwm_api);

DT_INST_FOREACH_STATUS_OKAY(MAX6639_PWM_INIT);
