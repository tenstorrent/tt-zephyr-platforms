/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/sensor.h>

static const struct device *const ina228 = DEVICE_DT_GET(DT_NODELABEL(ina228));

ZTEST(ina228_tests, test_ina228)
{
	struct sensor_value sensor_val;

	sensor_sample_fetch_chan(ina228, SENSOR_CHAN_POWER);
	sensor_channel_get(ina228, SENSOR_CHAN_POWER, &sensor_val);

	int16_t power = sensor_val.val1 & 0xFFFF;

	zassert_true(10 <= power && power <= 500);
}

ZTEST_SUITE(ina228_tests, NULL, NULL, NULL, NULL, NULL);
