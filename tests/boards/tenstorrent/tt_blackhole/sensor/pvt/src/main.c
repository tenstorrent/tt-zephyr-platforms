/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pvt/pvt_tt_bh.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

const struct device *const pvt = DEVICE_DT_GET(DT_NODELABEL(pvt));

SENSOR_DT_READ_IODEV(pvt_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS, 0});
RTIO_DEFINE(pvt_ctx, 1, 1);

ZTEST(pvt_tt_bh_tests, test_attr_get)
{
	struct sensor_value val;
	int ret;

	/* Test getting number of temperature sensors */
	ret = sensor_attr_get(pvt, 0, SENSOR_ATTR_PVT_TT_BH_NUM_TS, &val);
	zassert_ok(ret, "Should successfully get NUM_TS attribute");
	zassert_equal(val.val1, 16, "Should have 16 temperature sensors");
	zassert_equal(val.val2, 0, "val2 should be 0");

	/* Test getting number of voltage monitors */
	ret = sensor_attr_get(pvt, 0, SENSOR_ATTR_PVT_TT_BH_NUM_VM, &val);
	zassert_ok(ret, "Should successfully get NUM_VM attribute");
	zassert_equal(val.val1, 8, "Should have 8 voltage monitors");
	zassert_equal(val.val2, 0, "val2 should be 0");

	/* Test getting number of process detectors */
	ret = sensor_attr_get(pvt, 0, SENSOR_ATTR_PVT_TT_BH_NUM_PD, &val);
	zassert_ok(ret, "Should successfully get NUM_PD attribute");
	zassert_equal(val.val1, 8, "Should have 8 process detectors");
	zassert_equal(val.val2, 0, "val2 should be 0");

	/* Test invalid attribute */
	ret = sensor_attr_get(pvt, 0, 999, &val);
	zassert_equal(ret, -ENOTSUP, "Should return -ENOTSUP for invalid attribute");

	/* Test NULL pointer */
	ret = sensor_attr_get(pvt, 0, SENSOR_ATTR_PVT_TT_BH_NUM_TS, NULL);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL pointer");

	/* Test NULL device */
	ret = sensor_attr_get(NULL, 0, SENSOR_ATTR_PVT_TT_BH_NUM_TS, &val);
	zassert_equal(ret, -EINVAL, "Should return -EINVAL for NULL device");
}

ZTEST(pvt_tt_bh_tests, test_read)
{
	const struct sensor_decoder_api *decoder_api;

	sensor_get_decoder(pvt, &decoder_api);

	int rc;
	uint8_t buf[8];
	struct sensor_q31_data temp_data = {0};
	struct sensor_decode_context temp_decoder =
		SENSOR_DECODE_CONTEXT_INIT(decoder_api, buf, SENSOR_CHAN_PVT_TT_BH_TS, 0);

	rc = sensor_read(&pvt_iodev, &pvt_ctx, buf, sizeof(buf));
	zassert_ok(rc);

	rc = sensor_decode(&temp_decoder, &temp_data, 1);
	zassert_ok(rc);
}

ZTEST_SUITE(pvt_tt_bh_tests, NULL, NULL, NULL, NULL, NULL);
