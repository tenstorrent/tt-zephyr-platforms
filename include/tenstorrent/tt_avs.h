/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef TENSTORRENT_TT_AVS_H_
#define TENSTORRENT_TT_AVS_H_

#include <stdint.h>

#include <zephyr/device.h>

#define AVS_VCORE_RAIL  0
#define AVS_VCOREM_RAIL 1

typedef enum {
	AVSPwrModeMaxEff = 0,
	AVSPwrModeMaxPower = 3,
} AVSPwrMode;

struct avs_driver_api {
	int (*read_voltage)(const struct device *dev, uint8_t rail_sel, uint16_t *voltage_in_mV);
	int (*write_voltage)(const struct device *dev, uint16_t voltage_in_mV, uint8_t rail_sel);
	int (*read_vout_trans_rate)(const struct device *dev, uint8_t rail_sel, uint8_t *rise_rate,
				    uint8_t *fall_rate);
	int (*write_vout_trans_rate)(const struct device *dev, uint8_t rise_rate, uint8_t fall_rate,
				     uint8_t rail_sel);
	int (*read_current)(const struct device *dev, uint8_t rail_sel, float *current_in_A);
	int (*read_temp)(const struct device *dev, uint8_t rail_sel, float *temp_in_C);
	int (*force_voltage_reset)(const struct device *dev, uint8_t rail_sel);
	int (*read_power_mode)(const struct device *dev, uint8_t rail_sel, AVSPwrMode *power_mode);
	int (*write_power_mode)(const struct device *dev, AVSPwrMode power_mode, uint8_t rail_sel);
	int (*read_status)(const struct device *dev, uint8_t rail_sel, uint16_t *status);
	int (*write_status)(const struct device *dev, uint16_t status, uint8_t rail_sel);
	int (*read_version)(const struct device *dev, uint16_t *version);
	int (*read_system_input_current)(const struct device *dev, uint16_t *response);
};

static inline int avs_read_voltage(const struct device *dev, uint8_t rail_sel,
				   uint16_t *voltage_in_mV)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_voltage(dev, rail_sel, voltage_in_mV);
}

static inline int avs_write_voltage(const struct device *dev, uint16_t voltage_in_mV,
				    uint8_t rail_sel)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->write_voltage(dev, voltage_in_mV, rail_sel);
}

static inline int avs_read_vout_trans_rate(const struct device *dev, uint8_t rail_sel,
					   uint8_t *rise_rate, uint8_t *fall_rate)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_vout_trans_rate(dev, rail_sel, rise_rate, fall_rate);
}

static inline int avs_write_vout_trans_rate(const struct device *dev, uint8_t rise_rate,
					    uint8_t fall_rate, uint8_t rail_sel)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->write_vout_trans_rate(dev, rise_rate, fall_rate, rail_sel);
}

static inline int avs_read_current(const struct device *dev, uint8_t rail_sel, float *current_in_A)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_current(dev, rail_sel, current_in_A);
}

static inline int avs_read_temp(const struct device *dev, uint8_t rail_sel, float *temp_in_C)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_temp(dev, rail_sel, temp_in_C);
}

static inline int avs_force_voltage_reset(const struct device *dev, uint8_t rail_sel)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->force_voltage_reset(dev, rail_sel);
}

static inline int avs_read_power_mode(const struct device *dev, uint8_t rail_sel,
				      AVSPwrMode *power_mode)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_power_mode(dev, rail_sel, power_mode);
}

static inline int avs_write_power_mode(const struct device *dev, AVSPwrMode power_mode,
				       uint8_t rail_sel)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->write_power_mode(dev, power_mode, rail_sel);
}

static inline int avs_read_status(const struct device *dev, uint8_t rail_sel, uint16_t *status)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_status(dev, rail_sel, status);
}

static inline int avs_write_status(const struct device *dev, uint16_t status, uint8_t rail_sel)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->write_status(dev, status, rail_sel);
}

static inline int avs_read_version(const struct device *dev, uint16_t *version)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_version(dev, version);
}

static inline int avs_read_system_input_current(const struct device *dev, uint16_t *response)
{
	const struct avs_driver_api *api = (const struct avs_driver_api *)dev->api;

	return api->read_system_input_current(dev, response);
}

#endif /* TENSTORRENT_TT_AVS_H_ */
