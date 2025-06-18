/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/mfd/max6639.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

const struct device *get_pwm_device(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(max6639_pwm));
}

const struct device *get_sensor_device(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(max6639_sensor));
}

static void *max6639_basic_setup(void)
{
	const struct device *pwm_dev = get_pwm_device();
	const struct device *sensor_dev = get_sensor_device();

	zassert_true(device_is_ready(pwm_dev), "PWM device is not ready");
	k_object_access_grant(pwm_dev, k_current_get());

	zassert_true(device_is_ready(sensor_dev), "Sensor device is not ready");
	k_object_access_grant(sensor_dev, k_current_get());

	return NULL;
}

static void max6639_teardown(void *fixture)
{
	const struct device *pwm_dev = get_pwm_device();

	pwm_set_cycles(pwm_dev, 0, 120, 120, 0);
}

ZTEST(test_driver_maxim_max6639, test_set_read_rpm_and_duty_cycle)
{
	const struct device *dev = get_pwm_device();
	const struct device *sensor_dev = get_sensor_device();
	struct sensor_value data;
	int ret;

	ret = pwm_set_cycles(dev, 0, 120, 60, 0);
	zassert_equal(ret, 0, "Error setting 60/120");
	TC_PRINT("Set cycles of 60/120\n");

	k_sleep(K_MSEC(10000));

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_RPM);
	zassert_equal(ret, 0, "Error fetching RPM value");

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE);
	zassert_equal(ret, 0, "Error fetching duty cycle value");

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_TEMP);
	zassert_equal(ret, 0, "Error fetching temperature value");

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_RPM, &data);
	TC_PRINT("[RPM] = %d\n", data.val1);

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE, &data);
	TC_PRINT("[DUTY CYCLE] = %d\n", data.val1);

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_TEMP, &data);
	TC_PRINT("[TEMP C] = %d.%d\n\n", data.val1, data.val2);

	ret = pwm_set_cycles(dev, 0, 120, 120, 0);
	zassert_equal(ret, 0, "Error setting 120/120");

	TC_PRINT("Set cycles of 120/120\n");
	k_sleep(K_MSEC(10000));

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_RPM);
	zassert_equal(ret, 0, "Error fetching RPM value");

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE);
	zassert_equal(ret, 0, "Error fetching duty cycle value");

	ret = sensor_sample_fetch_chan(sensor_dev, MAX6639_CHAN_1_TEMP);
	zassert_equal(ret, 0, "Error fetching temperature value");

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_RPM, &data);
	TC_PRINT("[RPM] = %d\n", data.val1);

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_DUTY_CYCLE, &data);
	TC_PRINT("[DUTY CYCLE] = %d\n", data.val1);

	sensor_channel_get(sensor_dev, MAX6639_CHAN_1_TEMP, &data);
	TC_PRINT("[TEMP C] = %d.%d\n\n", data.val1, data.val2);

	k_sleep(K_MSEC(10000));
}

ZTEST_SUITE(test_driver_maxim_max6639, NULL, max6639_basic_setup, NULL, NULL, max6639_teardown);
