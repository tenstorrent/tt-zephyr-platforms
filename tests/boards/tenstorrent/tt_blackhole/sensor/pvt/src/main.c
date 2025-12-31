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

#define PD_ALL_INDICES                                                                             \
	{SENSOR_CHAN_PVT_TT_BH_PD, 0}, {SENSOR_CHAN_PVT_TT_BH_PD, 1},                              \
		{SENSOR_CHAN_PVT_TT_BH_PD, 2}, {SENSOR_CHAN_PVT_TT_BH_PD, 3},                      \
		{SENSOR_CHAN_PVT_TT_BH_PD, 4}, {SENSOR_CHAN_PVT_TT_BH_PD, 5},                      \
		{SENSOR_CHAN_PVT_TT_BH_PD, 6}, {SENSOR_CHAN_PVT_TT_BH_PD, 7},                      \
		{SENSOR_CHAN_PVT_TT_BH_PD, 8}, {SENSOR_CHAN_PVT_TT_BH_PD, 9},                      \
		{SENSOR_CHAN_PVT_TT_BH_PD, 10}, {SENSOR_CHAN_PVT_TT_BH_PD, 11},                    \
		{SENSOR_CHAN_PVT_TT_BH_PD, 12}, {SENSOR_CHAN_PVT_TT_BH_PD, 13},                    \
		{SENSOR_CHAN_PVT_TT_BH_PD, 14}, {SENSOR_CHAN_PVT_TT_BH_PD, 15}

#define VM_ALL_INDICES                                                                             \
	{SENSOR_CHAN_PVT_TT_BH_VM, 0}, {SENSOR_CHAN_PVT_TT_BH_VM, 1},                              \
		{SENSOR_CHAN_PVT_TT_BH_VM, 2}, {SENSOR_CHAN_PVT_TT_BH_VM, 3},                      \
		{SENSOR_CHAN_PVT_TT_BH_VM, 4}, {SENSOR_CHAN_PVT_TT_BH_VM, 5},                      \
		{SENSOR_CHAN_PVT_TT_BH_VM, 6}, {SENSOR_CHAN_PVT_TT_BH_VM, 7}

#define TS_ALL_INDICES                                                                             \
	{SENSOR_CHAN_PVT_TT_BH_TS, 0}, {SENSOR_CHAN_PVT_TT_BH_TS, 1},                              \
		{SENSOR_CHAN_PVT_TT_BH_TS, 2}, {SENSOR_CHAN_PVT_TT_BH_TS, 3},                      \
		{SENSOR_CHAN_PVT_TT_BH_TS, 4}, {SENSOR_CHAN_PVT_TT_BH_TS, 5},                      \
		{SENSOR_CHAN_PVT_TT_BH_TS, 6}, {SENSOR_CHAN_PVT_TT_BH_TS, 7}

SENSOR_DT_READ_IODEV(test_pd_iodev, DT_NODELABEL(pvt), PD_ALL_INDICES);
SENSOR_DT_READ_IODEV(test_vm_iodev, DT_NODELABEL(pvt), VM_ALL_INDICES);
SENSOR_DT_READ_IODEV(test_ts_iodev, DT_NODELABEL(pvt), TS_ALL_INDICES);
SENSOR_DT_READ_IODEV(test_ts_avg_iodev, DT_NODELABEL(pvt), TS_ALL_INDICES,
		     {SENSOR_CHAN_PVT_TT_BH_TS_AVG, 0});
SENSOR_DT_READ_IODEV(test_all_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_PD, 15},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 7}, {SENSOR_CHAN_PVT_TT_BH_TS, 7});

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
	q31_t freq_from_decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_PD, SENSOR_ATTR_PVT_TT_BH_NUM_PD,
			      &pd_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	ret = sensor_read(&test_pd_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	for (int i = 0; i < pd_count.val1; ++i) {
		float freq_from_manual = pvt_tt_bh_raw_to_freq(test_buf[i].raw);

		decoder->decode((const uint8_t *)test_buf,
				(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD, i}, NULL,
				pd_count.val1, &freq_from_decoder);

		zassert_equal(freq_from_manual, Q31_TO_FREQ(freq_from_decoder),
			      "Decoder frequency %d differs from manual decoding %d",
			      (int)freq_from_manual, (int)Q31_TO_FREQ(freq_from_decoder));
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
	q31_t volt_from_decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_VM, SENSOR_ATTR_PVT_TT_BH_NUM_VM,
			      &vm_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	pvt_tt_bh_delay_chain_set(1);
	ret = sensor_read(&test_vm_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	for (int i = 0; i < vm_count.val1; ++i) {
		float volt_from_manual = pvt_tt_bh_raw_to_volt(test_buf[i].raw);

		decoder->decode((const uint8_t *)test_buf,
				(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM, i}, NULL,
				vm_count.val1, &volt_from_decoder);

		zassert_equal(volt_from_manual, Q31_TO_VOLT(volt_from_decoder),
			      "Decoder voltage %d differs from manual decoding %d",
			      (int)volt_from_manual, (int)Q31_TO_VOLT(volt_from_decoder));
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
	q31_t temp_from_decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_attr_get(pvt, SENSOR_CHAN_PVT_TT_BH_TS, SENSOR_ATTR_PVT_TT_BH_NUM_TS,
			      &ts_count);
	zassert_ok(ret, "sensor_attr_get failed with %d", ret);

	ret = sensor_read(&test_ts_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	for (int i = 0; i < ts_count.val1; ++i) {
		float temp_from_manual = pvt_tt_bh_raw_to_temp(test_buf[i].raw);

		decoder->decode((const uint8_t *)test_buf,
				(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, i}, NULL,
				ts_count.val1, &temp_from_decoder);

		zassert_equal(temp_from_manual, Q31_TO_TEMP(temp_from_decoder),
			      "Decoder temperature %d differs from manual decoding %d",
			      (int)temp_from_manual, (int)Q31_TO_TEMP(temp_from_decoder));
	}
}

/*
 * Test read and decode temperature sensor average.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_ts_avg)
{
	const struct sensor_decoder_api *decoder;
	q31_t celcius;
	q31_t celcius_from_avg_channel;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	ret = sensor_read(&test_ts_avg_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	/* Calculate manual average from individual temperature sensors */
	float avg_tmp = 0;

	for (uint8_t i = 0; i < 8; i++) {
		decoder->decode((const uint8_t *)test_buf,
				(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, i}, NULL, 9,
				&celcius);
		avg_tmp += Q31_TO_TEMP(celcius);
	}

	avg_tmp /= 8;

	/* Get celcius value from average channel decoder */
	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS_AVG, 0}, NULL, 9,
			&celcius_from_avg_channel);

	/* Assert celcius temperature from manual average and average channel are equal */
	zassert_within(avg_tmp, Q31_TO_TEMP(celcius_from_avg_channel), AVG_TEMP_TOLERANCE,
		       "Integral part of celcius from average channel %d is not equal to celcius "
		       "from manual average %d",
		       (int)Q31_TO_TEMP(celcius_from_avg_channel), (int)avg_tmp);
}

/*
 * Test read and decode all three sensors.
 *
 * After the read call, manually read and convert the data from the test_buffer to
 * respective units and compare against the values returned by the decoder.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_all)
{
	q31_t from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "sensor_get_decoder failed with %d", ret);

	pvt_tt_bh_delay_chain_set(1);

	ret = sensor_read(&test_all_iodev, &test_pvt_ctx, (uint8_t *)test_buf, sizeof(test_buf));
	zassert_ok(ret, "sensor_read failed with %d", ret);

	/* Test PD (Process Detector) - index 0 */
	float converted_freq = pvt_tt_bh_raw_to_freq(test_buf[0].raw);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD, 15}, NULL, 3,
			&from_decoder);

	zassert_equal(converted_freq, Q31_TO_FREQ(from_decoder),
		      "PD: Integral part of frequency from decoder %d is not equal to frequency "
		      "from manual %d",
		      (int)Q31_TO_FREQ(from_decoder), (int)converted_freq);

	/* Test VM (Voltage Monitor) - index 1 */
	float converted_volt = pvt_tt_bh_raw_to_volt(test_buf[1].raw);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM, 7}, NULL, 3,
			&from_decoder);

	zassert_equal(converted_volt, Q31_TO_VOLT(from_decoder),
		      "VM: Integral part of voltage from decoder %d is not equal to voltage "
		      "from manual %d",
		      (int)Q31_TO_VOLT(from_decoder), (int)converted_volt);

	/* Test TS (Temperature Sensor) - index 2 */
	float converted_temp = pvt_tt_bh_raw_to_temp(test_buf[2].raw);

	decoder->decode((const uint8_t *)test_buf,
			(struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, 7}, NULL, 3,
			&from_decoder);

	zassert_equal(converted_temp, Q31_TO_TEMP(from_decoder),
		      "TS: Integral part of celsius from decoder %d is not equal to celsius "
		      "from manual %d",
		      (int)Q31_TO_TEMP(from_decoder), (int)converted_temp);
}

ZTEST_SUITE(pvt_tt_bh_tests, NULL, NULL, NULL, NULL, NULL);
