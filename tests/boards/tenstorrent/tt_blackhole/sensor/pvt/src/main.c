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

LOG_MODULE_REGISTER(test_pvt, LOG_LEVEL_DBG);

#define NUM_READS 5

/*
 * The tolerance in degrees when computing the average temperature, as
 * the average temperature of the chip could vary by one degree by the
 * time the average is computed.
 */
#define AVG_TEMP_TOLERANCE 1.0f

const struct device *const pvt = DEVICE_DT_GET(DT_NODELABEL(pvt));

SENSOR_DT_READ_IODEV(test_pd_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_PD});
SENSOR_DT_READ_IODEV(test_vm_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_VM});
SENSOR_DT_READ_IODEV(test_ts_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS});

/* PVT driver only supports one shot submissions, so use rtio size 1,1 */
RTIO_DEFINE(test_pvt_ctx, 1, 1);

/*
 * Used for storing read data in the read_decode test.
 * Each read is a `struct sensor_value`, and the test does NUM_READ reads.
 */
static struct pvt_tt_bh_rtio_data test_buf[16];

ZTEST(pvt_tt_bh_tests, test_attr_get)
{
	struct sensor_value val;
	int ret;

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_TS, SENSOR_ATTR_PVT_TT_BH_NUM_TS, &val);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);
	zassert_equal(val.val1, 8, "Should have 8 temperature sensors");
	zassert_equal(val.val2, 0);
}

/*
 * Test read and decode process detector.
 *
 * After the read call, compare the frequency returned by the decoder against
 * manually decoding the output buffer. Manually decoding the output buffer is
 * the source of truth.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_pd)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_value pd_count;
	uint16_t freq_from_decoder[16];
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_PD, SENSOR_ATTR_PVT_TT_BH_NUM_PD,
			      &pd_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	ret = sensor_read(&test_pd_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD}, NULL,
			pd_count.val1, freq_from_decoder);

	for (int i = 0; i < pd_count.val1; ++i) {
		float freq_from_manual = pvt_tt_bh_raw_to_freq(test_buf[i].raw);

		zassert_equal(freq_from_manual, pvt_tt_bh_raw_to_freq(freq_from_decoder[i]),
			      "Decoder frequency %d differs from manual decoding %d",
			      (int)freq_from_manual, (int)pvt_tt_bh_raw_to_freq(freq_from_decoder[i]));
	}
}

/*
 * Test read and decode voltage monitor.
 *
 * After the read call, compare the voltage returned by the decoder against
 * manually decoding the output buffer. Manually decoding the output buffer is
 * the source of truth.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_vm)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_value vm_count;
	uint16_t volt_from_decoder[8];
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_VM, SENSOR_ATTR_PVT_TT_BH_NUM_VM,
			      &vm_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	pvt_tt_bh_delay_chain_set(1);
	ret = sensor_read(&test_vm_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM}, NULL,
			vm_count.val1, volt_from_decoder);

	for (int i = 0; i < vm_count.val1; ++i) {
		float volt_from_manual = pvt_tt_bh_raw_to_volt(test_buf[i].raw);

		zassert_equal(volt_from_manual, pvt_tt_bh_raw_to_volt(volt_from_decoder[i]),
			      "Decoder voltage %d differs from manual decoding %d",
			      (int)volt_from_manual, (int)pvt_tt_bh_raw_to_volt(volt_from_decoder[i]));
	}
}

/*
 * Test read and decode temperature sensor.
 *
 * After the read call, compare the temperature returned by the decoder against
 * manually decoding the output buffer. Manually decoding the output buffer is
 * the source of truth.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_ts)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_value ts_count;
	uint16_t temp_from_decoder[8];
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_TS, SENSOR_ATTR_PVT_TT_BH_NUM_TS,
			      &ts_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	ret = sensor_read(&test_ts_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS}, NULL,
			ts_count.val1, temp_from_decoder);

	for (int i = 0; i < ts_count.val1; ++i) {
		float temp_from_manual = pvt_tt_bh_raw_to_temp(test_buf[i].raw);

		zassert_equal(temp_from_manual, pvt_tt_bh_raw_to_temp(temp_from_decoder[i]),
			      "Decoder temperature %d differs from manual decoding %d",
			      (int)temp_from_manual, (int)pvt_tt_bh_raw_to_temp(temp_from_decoder[i]));
	}
}

ZTEST_SUITE(pvt_tt_bh_tests, NULL, NULL, NULL, NULL, NULL);
