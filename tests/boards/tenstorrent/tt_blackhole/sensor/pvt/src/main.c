/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/tenstorrent/pvt_tt_bh.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>

#include <math.h> /* roundf */

LOG_MODULE_REGISTER(test_pvt, LOG_LEVEL_DBG);

const struct device *const pvt = DEVICE_DT_GET(DT_NODELABEL(pvt));

SENSOR_DT_READ_IODEV(pvt_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS, 0});

RTIO_DEFINE(pvt_ctx, 1, 1);

uint8_t buf[32];

static inline void float_to_sensor_value(float f, struct sensor_value *val)
{
	val->val1 = (int32_t)f;
	val->val2 = (int32_t)roundf((f - (float)val->val1) * 1000000.0f);

	/* Handle carry/borrow if val2 rounded to 1e6 or -1e6 */
	if (val->val2 >= 1000000) {
		val->val1 += 1;
		val->val2 -= 1000000;
	} else if (val->val2 <= -1000000) {
		val->val1 -= 1;
		val->val2 += 1000000;
	}
}

ZTEST(pvt_tt_bh_tests, test_attr_get)
{
	struct sensor_value val;
	int ret;

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_TS, SENSOR_ATTR_PVT_TT_BH_NUM_TS, &val);
	zassert_ok(ret);
	zassert_equal(val.val1, 8, "Should have 8 temperature sensors");
	zassert_equal(val.val2, 0);
}

/*
 * Test multiple consecutive read and decode operations.
 *
 * After the read call, manually read and convert the data from the buffer to
 * celcius and compare against the celcius value returned by the decocder.
 */
ZTEST(pvt_tt_bh_tests, test_multiple_read_decode)
{
	struct sensor_value celcius_from_manual;
	struct sensor_value celcius_from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret);

	/* Perform multiple read and decode operations */
	for (int i = 0; i < 5; i++) {
		ret = sensor_read(&pvt_iodev, &pvt_ctx, buf, sizeof(buf));
		zassert_ok(ret);

		/* Get celcius value by manually converting buffer */

		uint16_t raw_temp = ((uint16_t)buf[1] << 8) | buf[0];
		float eqbs = raw_temp / 4096.0 - 0.5;
		float converted_temp = 83.09f + 262.5f * eqbs;

		float_to_sensor_value(converted_temp, &celcius_from_manual);
		LOG_DBG("raw temp: %d ", raw_temp);
		LOG_DBG("celcius from manual: %d.%d", celcius_from_manual.val1,
			celcius_from_manual.val2);

		/* Get celcius value from decoder */
		decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, 0}, NULL,
				1, &celcius_from_decoder);
		LOG_DBG("celcius from decoder: %d.%d\n", celcius_from_decoder.val1,
			celcius_from_decoder.val2);

		/* Assert celcius tempertaure from manual read and decoder are equal. */
		zassert_equal(celcius_from_manual.val1, celcius_from_decoder.val1,
			      "Integral part of celcius from decoder %d is not equal to celcius "
			      "from manual %d",
			      celcius_from_manual.val1, celcius_from_decoder.val1);

		/* Check val2 with tolerance of 0.001 degrees. */
		zassert_within(celcius_from_manual.val2, celcius_from_decoder.val2, 1000,
			       "Floating part of celcius from decoder %d is not within 0.001 of "
			       "celcius from manual %d",
			       celcius_from_decoder.val2, celcius_from_manual.val2);
	}
}

ZTEST_SUITE(pvt_tt_bh_tests, NULL, NULL, NULL, NULL, NULL);
