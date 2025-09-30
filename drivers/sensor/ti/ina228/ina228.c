/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_ina228

#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/sys/util_macro.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

/* Device register addresses. */
#define INA228_REG_CONFIG          0x00
#define INA228_REG_ADC_CONFIG      0x01
#define INA228_REG_SHUNT_CAL       0x02
#define INA228_REG_SHUNT_TEMPCO    0x03
#define INA228_REG_VSHUNT          0x04
#define INA228_REG_VBUS            0x05
#define INA228_REG_DIETEMP         0x06
#define INA228_REG_CURRENT         0x07
#define INA228_REG_POWER           0x08
#define INA228_REG_ENERGY          0x09
#define INA228_REG_CHARGE          0x0A
#define INA228_REG_DIAG_ALRT       0x0B
#define INA228_REG_SOVL            0x0C
#define INA228_REG_BOVL            0x0D
#define INA228_REG_BUVL            0x0E
#define INA228_REG_TEMP_LIMIT      0x10
#define INA228_REG_PWR_LIMIT       0x11
#define INA228_REG_MANUFACTURER_ID 0x3E
#define INA228_REG_DEVICE_ID       0x3F

/* Device register values. */
#define INA228_MANUFACTURER_ID 0x5449
#define INA228_DEVICE_ID       0x228

#define INA228_ADC_RANGE BIT(4)

struct ina228_data {
	const struct device *dev;
	int32_t current;
	uint32_t bus_voltage;
	uint32_t power;
	uint16_t temp;
	int32_t shunt_voltage;
	enum sensor_channel chan;
};

struct ina228_config {
	const struct i2c_dt_spec bus;
	uint16_t config;
	uint16_t adc_config;
	uint32_t current_lsb;
	uint16_t cal;
};

LOG_MODULE_REGISTER(INA228, CONFIG_SENSOR_LOG_LEVEL);

/** @brief Shunt calibration scaling value (scaled by 10^5) */
#define INA228_SHUNT_CAL_SCALING 131072ULL

/** @brief Power scaling value */
#define INA228_POWER_SCALING(x) ((x) * 32ULL / 10ULL)

/** @brief The conversion factor for the bus voltage register, in microvolts/LSB. */
#define INA228_BUS_VOLTAGE_TO_uV(x) ((x) * 1953125ULL / 10000ULL)

/** @brief The conversion factor for the shunt voltage register, in nanovolts/LSB. */
#define INA228_SHUNT_VOLTAGE_TO_nV_0(x) ((x) * 3125U / 10U)      /* 312.5 nV/LSB: ADCRANGE=0 */
#define INA228_SHUNT_VOLTAGE_TO_nV_1(x) ((x) * 78125UL / 1000UL) /* 78.125 nV/LSB: ADCRANGE=1 */

/** @brief The conversion factor for the die temperature register, in millidegrees C/LSB */
#define INA228_TEMP_TO_mdegC(x) ((x) * 78125UL / 10000UL)

int ina228_reg_read_16(const struct i2c_dt_spec *bus, uint8_t reg, uint16_t *val)
{
	uint8_t data[2];
	int ret;

	ret = i2c_burst_read_dt(bus, reg, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	*val = sys_get_be16(data);

	return ret;
}

int ina228_reg_read_24(const struct i2c_dt_spec *bus, uint8_t reg, uint32_t *val)
{
	uint8_t data[3];
	int ret;

	ret = i2c_burst_read_dt(bus, reg, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	*val = sys_get_be24(data);

	return ret;
}

int ina228_reg_read_40(const struct i2c_dt_spec *bus, uint8_t reg, uint64_t *val)
{
	uint8_t data[5];
	int ret;

	ret = i2c_burst_read_dt(bus, reg, data, sizeof(data));
	if (ret < 0) {
		return ret;
	}

	*val = sys_get_be40(data);

	return ret;
}

/* 16 bit write */
int ina228_reg_write(const struct i2c_dt_spec *bus, uint8_t reg, uint16_t val)
{
	uint8_t tx_buf[3];

	tx_buf[0] = reg;
	sys_put_be16(val, &tx_buf[1]);

	return i2c_write_dt(bus, tx_buf, sizeof(tx_buf));
}

static void micro_s32_to_sensor_value(struct sensor_value *val, int32_t value_microX)
{
	val->val1 = value_microX / 1000000L;
	val->val2 = value_microX % 1000000L;
}

static int ina228_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	const struct ina228_data *data = dev->data;
	const struct ina228_config *config = dev->config;

	switch (chan) {
	case SENSOR_CHAN_VOLTAGE:
		micro_s32_to_sensor_value(val, INA228_BUS_VOLTAGE_TO_uV(data->bus_voltage >> 4));
		break;
	case SENSOR_CHAN_CURRENT:
		/* see datasheet "Current, Power, Energy, and Charge Calculations" section */
		micro_s32_to_sensor_value(val, (data->current >> 4) * config->current_lsb);
		break;
	case SENSOR_CHAN_POWER:
		micro_s32_to_sensor_value(val,
					  INA228_POWER_SCALING(data->power * config->current_lsb));
		break;
	case SENSOR_CHAN_VSHUNT:
		if (IS_ENABLED(CONFIG_INA228_VSHUNT)) {
			if (config->adc_config & INA228_ADC_RANGE) {
				micro_s32_to_sensor_value(val, INA228_SHUNT_VOLTAGE_TO_nV_1(
								       data->shunt_voltage >> 4));
			} else {
				micro_s32_to_sensor_value(val, INA228_SHUNT_VOLTAGE_TO_nV_0(
								       data->shunt_voltage >> 4));
			}
		} else {
			return -ENOTSUP;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int ina228_read_data(const struct device *dev)
{
	struct ina228_data *data = dev->data;
	const struct ina228_config *config = dev->config;
	int ret;

	if ((data->chan == SENSOR_CHAN_ALL) || (data->chan == SENSOR_CHAN_VOLTAGE)) {
		ret = ina228_reg_read_24(&config->bus, INA228_REG_VBUS, &data->bus_voltage);
		if (ret < 0) {
			LOG_ERR("Failed to read bus voltage");
			return ret;
		}
	}

	if ((data->chan == SENSOR_CHAN_ALL) || (data->chan == SENSOR_CHAN_CURRENT)) {
		ret = ina228_reg_read_24(&config->bus, INA228_REG_CURRENT, &data->current);
		if (ret < 0) {
			LOG_ERR("Failed to read current");
			return ret;
		}
	}

	if ((data->chan == SENSOR_CHAN_ALL) || (data->chan == SENSOR_CHAN_POWER)) {
		ret = ina228_reg_read_24(&config->bus, INA228_REG_POWER, &data->power);
		if (ret < 0) {
			LOG_ERR("Failed to read power");
			return ret;
		}
	}

	if ((data->chan == SENSOR_CHAN_ALL) || (data->chan == SENSOR_CHAN_DIE_TEMP)) {
		ret = ina228_reg_read_16(&config->bus, INA228_REG_DIETEMP, &data->temp);
		if (ret < 0) {
			LOG_ERR("Failed to read die temp");
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_INA228_VSHUNT)) {
		if ((data->chan == SENSOR_CHAN_ALL) || (data->chan == SENSOR_CHAN_VSHUNT)) {
			ret = ina228_reg_read_24(&config->bus, INA228_REG_VSHUNT,
						 &data->shunt_voltage);
			if (ret < 0) {
				LOG_ERR("Failed to read shunt voltage");
				return ret;
			}
		}
	}

	return 0;
}

static int ina228_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct ina228_data *data = dev->data;

	if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_VOLTAGE && chan != SENSOR_CHAN_CURRENT &&
	    chan != SENSOR_CHAN_POWER &&
	    (IS_ENABLED(CONFIG_INA228_VSHUNT) ? chan != SENSOR_CHAN_VSHUNT : true) &&
	    chan != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	data->chan = chan;

	return ina228_read_data(dev);
}

static int ina228_attr_set(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, const struct sensor_value *val)
{
	const struct ina228_config *config = dev->config;
	uint16_t data = val->val1;

	switch (attr) {
	case SENSOR_ATTR_CONFIGURATION:
		return ina228_reg_write(&config->bus, INA228_REG_CONFIG, data);
	case SENSOR_ATTR_CALIBRATION:
		return ina228_reg_write(&config->bus, INA228_REG_SHUNT_CAL, data);
	default:
		LOG_ERR("INA228 attribute not supported.");
		return -ENOTSUP;
	}
}

static int ina228_attr_get(const struct device *dev, enum sensor_channel chan,
			   enum sensor_attribute attr, struct sensor_value *val)
{
	const struct ina228_config *config = dev->config;
	uint16_t data;
	int ret;

	switch (attr) {
	case SENSOR_ATTR_CONFIGURATION:
		ret = ina228_reg_read_16(&config->bus, INA228_REG_CONFIG, &data);
		if (ret < 0) {
			return ret;
		}
		break;
	case SENSOR_ATTR_CALIBRATION:
		ret = ina228_reg_read_16(&config->bus, INA228_REG_SHUNT_CAL, &data);
		if (ret < 0) {
			return ret;
		}
		break;
	default:
		LOG_ERR("INA228 attribute not supported.");
		return -ENOTSUP;
	}

	val->val1 = data;
	val->val2 = 0;

	return 0;
}

static int ina228_shunt_calibrate(const struct device *dev)
{
	const struct ina228_config *config = dev->config;
	uint16_t shunt_cal = config->cal;
	int ret;

	/* For ADC_RANGE = 1 the value of SHUNT_CAL must be multiplied by 4 */
	if (config->adc_config & INA228_ADC_RANGE) {
		shunt_cal *= 4;
	}

	ret = ina228_reg_write(&config->bus, INA228_REG_SHUNT_CAL, shunt_cal);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int ina228_init(const struct device *dev)
{
	struct ina228_data *data = dev->data;
	const struct ina228_config *config = dev->config;
	uint16_t id;
	int ret;

	if (!i2c_is_ready_dt(&config->bus)) {
		LOG_ERR("I2C bus %s is not ready", config->bus.bus->name);
		return -ENODEV;
	}

	data->dev = dev;

	ret = ina228_reg_read_16(&config->bus, INA228_REG_MANUFACTURER_ID, &id);
	if (ret < 0) {
		LOG_ERR("Failed to read manufacturer register.");
		return ret;
	}

	if (id != INA228_MANUFACTURER_ID) {
		LOG_ERR("Manufacturer ID doesn't match.");
		return -ENODEV;
	}

	ret = ina228_reg_read_16(&config->bus, INA228_REG_DEVICE_ID, &id);
	if (ret < 0) {
		LOG_ERR("Failed to read device register.");
		return ret;
	}

	if (id >> 4 != INA228_DEVICE_ID) {
		LOG_ERR("Device ID doesn't match.");
		return -ENODEV;
	}

	ret = ina228_reg_write(&config->bus, INA228_REG_CONFIG, config->config);
	if (ret < 0) {
		LOG_ERR("Failed to write configuration register.");
		return ret;
	}

	ret = ina228_reg_write(&config->bus, INA228_REG_ADC_CONFIG, config->adc_config);
	if (ret < 0) {
		LOG_ERR("Failed to write ADC configuration register.");
		return ret;
	}

	ret = ina228_shunt_calibrate(dev);
	if (ret < 0) {
		LOG_ERR("Failed to write calibration register.");
		return ret;
	}

	return 0;
}

static const struct sensor_driver_api ina228_driver_api = {
	.attr_set = ina228_attr_set,
	.attr_get = ina228_attr_get,
	.sample_fetch = ina228_sample_fetch,
	.channel_get = ina228_channel_get,
};

#define INA228_DRIVER_INIT(inst)                                                                   \
	static struct ina228_data ina228_data_##inst;                                              \
	static const struct ina228_config ina228_config_##inst = {                                 \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.current_lsb = DT_INST_PROP(inst, current_lsb_microamps),                          \
		.cal = INA228_SHUNT_CAL_SCALING * DT_INST_PROP(inst, current_lsb_microamps) *      \
		       DT_INST_PROP(inst, rshunt_micro_ohms) / 10000000ULL,                        \
		.config = (DT_INST_ENUM_IDX(inst, adc_conversion_delay) << 6) |                    \
			  (DT_INST_PROP(inst, shunt_temp_comp_en) << 5) |                          \
			  (DT_INST_PROP(inst, high_precision) << 4),                               \
		.adc_config = (DT_INST_ENUM_IDX(inst, adc_mode) << 12) |                           \
			      (DT_INST_ENUM_IDX(inst, vbus_conversion_time_us) << 9) |             \
			      (DT_INST_ENUM_IDX(inst, vshunt_conversion_time_us) << 6) |           \
			      (DT_INST_ENUM_IDX(inst, temp_conversion_time_us) << 3) |             \
			      DT_INST_ENUM_IDX(inst, avg_count)};                                  \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, &ina228_init, NULL, &ina228_data_##inst,                \
				     &ina228_config_##inst, POST_KERNEL,                           \
				     CONFIG_SENSOR_INIT_PRIORITY, &ina228_driver_api);

DT_INST_FOREACH_STATUS_OKAY(INA228_DRIVER_INIT)
