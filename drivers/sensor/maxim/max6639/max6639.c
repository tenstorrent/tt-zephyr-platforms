/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max6639_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/max6639.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

struct max6639_sensor_config {
	const struct i2c_dt_spec i2c;
};

struct max6639_sensor_data {
	uint8_t channel_1_tach;
	uint8_t channel_1_duty_cycle;
	uint8_t channel_1_temp;
	uint8_t channel_1_temp_extended;
	uint8_t channel_2_tach;
	uint8_t channel_2_duty_cycle;
	uint8_t channel_2_temp;
	uint8_t channel_2_temp_extended;
};

LOG_MODULE_REGISTER(max6639_sensor, CONFIG_SENSOR_LOG_LEVEL);

static int max6639_sensor_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct max6639_sensor_config *config = dev->config;
	struct max6639_sensor_data *data = dev->data;
	int result;

	switch ((enum max6639_sensor_channel)chan) {
	case MAX6639_CHAN_1_RPM:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_1_TACH,
					      &data->channel_1_tach);
		return result;
	case MAX6639_CHAN_1_DUTY_CYCLE:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_1_DUTY_CYCLE,
					      &data->channel_1_duty_cycle);
		return result;
	case MAX6639_CHAN_1_TEMP:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_1_TEMP_EXTENDED,
					      &data->channel_1_temp_extended);
		if (result != 0) {
			return result;
		}

		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_1_TEMP,
					      &data->channel_1_temp);
		return result;
	case MAX6639_CHAN_2_RPM:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_2_TACH,
					      &data->channel_2_tach);
		return result;
	case MAX6639_CHAN_2_DUTY_CYCLE:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_2_DUTY_CYCLE,
					      &data->channel_2_duty_cycle);
		return result;
	case MAX6639_CHAN_2_TEMP:
		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_2_TEMP_EXTENDED,
					      &data->channel_2_temp_extended);
		if (result != 0) {
			return result;
		}

		result = i2c_reg_read_byte_dt(&config->i2c, MAX6639_REG_CHANNEL_2_TEMP,
					      &data->channel_2_temp);
		return result;
	default:
		return -EINVAL;
	}
}

static int max6639_sensor_channel_get(const struct device *dev, enum sensor_channel chan,
				      struct sensor_value *val)
{
	struct max6639_sensor_data *data = dev->data;

	switch ((enum max6639_sensor_channel)chan) {
	case MAX6639_CHAN_1_RPM:
		val->val1 = MAX6639_RPM_RANGE * 30 / data->channel_1_tach;
		val->val2 = 0;
		return 0;
	case MAX6639_CHAN_1_DUTY_CYCLE:
		val->val1 = data->channel_1_duty_cycle / 1.2;
		val->val2 = 0;
		return 0;
	case MAX6639_CHAN_1_TEMP:
		val->val1 = data->channel_1_temp;
		val->val2 = (data->channel_1_temp_extended >> MAX6639_EXTENDED_TEMP_SHIFT) * 125;
		return 0;
	case MAX6639_CHAN_2_RPM:
		val->val1 = MAX6639_RPM_RANGE * 30 / data->channel_2_tach;
		val->val2 = 0;
		return 0;
	case MAX6639_CHAN_2_DUTY_CYCLE:
		val->val1 = data->channel_2_duty_cycle / 1.2;
		val->val2 = 0;
		return 0;
	case MAX6639_CHAN_2_TEMP:
		val->val1 = data->channel_2_temp;
		val->val2 = (data->channel_2_temp_extended >> MAX6639_EXTENDED_TEMP_SHIFT) * 125;
		return 0;
	default:
		return -EINVAL;
	}
}

static int max6639_sensor_init(const struct device *dev)
{
	const struct max6639_sensor_config *config = dev->config;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}

	return 0;
}

static DEVICE_API(sensor, max6639_sensor_api) = {
	.sample_fetch = max6639_sensor_sample_fetch,
	.channel_get = max6639_sensor_channel_get,
};

#define MAX6639_SENSOR_INIT(inst)                                                                  \
	static struct max6639_sensor_data max6639_sensor_##inst##_data;                            \
	static const struct max6639_sensor_config max6639_sensor_##inst##_config = {               \
		.i2c = I2C_DT_SPEC_GET(DT_INST_PARENT(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, max6639_sensor_init, NULL, &max6639_sensor_##inst##_data,      \
			      &max6639_sensor_##inst##_config, POST_KERNEL,                        \
			      CONFIG_MAX6639_SENSOR_INIT_PRIORITY, &max6639_sensor_api);

DT_INST_FOREACH_STATUS_OKAY(MAX6639_SENSOR_INIT);
