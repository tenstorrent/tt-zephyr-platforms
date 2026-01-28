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
#define AVG_TEMP_TOLERANCE 1

const struct device *const pvt = DEVICE_DT_GET(DT_NODELABEL(pvt));

SENSOR_DT_READ_IODEV(test_pd_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_PD, 0},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 1}, {SENSOR_CHAN_PVT_TT_BH_PD, 2},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 3}, {SENSOR_CHAN_PVT_TT_BH_PD, 4});

SENSOR_DT_READ_IODEV(test_vm_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_VM, 0},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 1}, {SENSOR_CHAN_PVT_TT_BH_VM, 2},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 3}, {SENSOR_CHAN_PVT_TT_BH_VM, 4});

SENSOR_DT_READ_IODEV(test_ts_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS, 0},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 1}, {SENSOR_CHAN_PVT_TT_BH_TS, 2},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 3}, {SENSOR_CHAN_PVT_TT_BH_TS, 4});

SENSOR_DT_READ_IODEV(test_all_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_PD, 15},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 7}, {SENSOR_CHAN_PVT_TT_BH_TS, 7});

SENSOR_DT_READ_IODEV(ts_ts_avg_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS, 0},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 1}, {SENSOR_CHAN_PVT_TT_BH_TS, 2},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 3}, {SENSOR_CHAN_PVT_TT_BH_TS, 4},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 5}, {SENSOR_CHAN_PVT_TT_BH_TS, 6},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 7}, {SENSOR_CHAN_PVT_TT_BH_TS_AVG, 0});

RTIO_DEFINE(test_pvt_ctx, NUM_READS, NUM_READS);

/*
 * Used for storing read data in the read_decode test.
 * Each read is a `struct sensor_value`, and the test does NUM_READ reads.
 */
static uint8_t test_buf[sizeof(struct sensor_value) * 9];

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
 * Test read and decode process detector.
 *
 * After the read call, manually read and convert the data from the test_buffer to
 * celcius and compare against the celcius value returned by the decocder.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_pd)
{
	float freq_from_manual;
	float freq_from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "Get decoder failed with %d", ret);

	ret = sensor_read(&test_pd_iodev, &test_pvt_ctx, test_buf, sizeof(test_buf));
	zassert_ok(ret, "Sensor read failed with %d", ret);

	for (int i = 0; i < NUM_READS; i++) {
		const struct pvt_tt_bh_rtio_data *raw_freq =
			&(((const struct pvt_tt_bh_rtio_data *)test_buf)[i]);

		freq_from_manual = pvt_tt_bh_raw_to_freq(raw_freq->raw);

		/* Get celcius value from decoder */
		decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD, i},
				NULL, NUM_READS, &freq_from_decoder);

		/* Assert celcius tempertaure from manual read and decoder are equal. */
		zassert_equal(freq_from_decoder, freq_from_manual);
	}
}

/*
 * Test read and decode voltage monitor.
 *
 * After the read call, manually read and convert the data from the test_buffer to
 * celcius and compare against the celcius value returned by the decocder.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_vm)
{
	float volt_from_manual;
	float volt_from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "Get decoder failed with %d", ret);

	pvt_tt_bh_delay_chain_set(1);

	ret = sensor_read(&test_vm_iodev, &test_pvt_ctx, test_buf, sizeof(test_buf));
	zassert_ok(ret, "Sensor read failed with %d", ret);

	for (int i = 0; i < NUM_READS; i++) {
		const struct pvt_tt_bh_rtio_data *raw_volt =
			&(((const struct pvt_tt_bh_rtio_data *)test_buf)[i]);

		volt_from_manual = pvt_tt_bh_raw_to_volt(raw_volt->raw);

		/* Get celcius value from decoder */
		decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM, i},
				NULL, NUM_READS, &volt_from_decoder);

		/* Assert celcius tempertaure from manual read and decoder are equal. */
		zassert_equal(volt_from_manual, volt_from_decoder);
	}
}

/*
 * Test read and decode temperature sensor.
 *
 * After the read call, manually read and convert the data from the test_buffer to
 * celcius and compare against the celcius value returned by the decocder.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_ts)
{
	float celcius_from_manual;
	float celcius_from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "Get decoder failed with %d", ret);

	ret = sensor_read(&test_ts_iodev, &test_pvt_ctx, test_buf, sizeof(test_buf));
	zassert_ok(ret, "Sensor read failed with %d", ret);

	for (int i = 0; i < NUM_READS; i++) {
		/* Get celcius value by manually converting test_buffer */

		const struct pvt_tt_bh_rtio_data *raw_temp =
			&(((const struct pvt_tt_bh_rtio_data *)test_buf)[i]);

		celcius_from_manual = pvt_tt_bh_raw_to_temp(raw_temp->raw);

		/* Get celcius value from decoder */
		decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, i},
				NULL, NUM_READS, &celcius_from_decoder);

		/* Assert celcius tempertaure from manual read and decoder are equal. */
		zassert_equal(celcius_from_decoder, celcius_from_manual);
	}
}

/*
 * Test read and decode temperature sensor average.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_ts_avg)
{
	float celcius;
	float celcius_from_manual_avg;
	float celcius_from_avg_channel;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "Get decoder failed with %d", ret);

	ret = sensor_read(&ts_ts_avg_iodev, &test_pvt_ctx, test_buf, sizeof(test_buf));
	zassert_ok(ret, "Sensor read failed with %d", ret);

	/* Calculate manual average from individual temperature sensors */
	float avg_tmp = 0;

	for (uint8_t i = 0; i < 8; i++) {
		decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, i},
				NULL, 9, &celcius);
		avg_tmp += celcius;
	}

	avg_tmp /= 8;
	celcius_from_manual_avg = avg_tmp;

	/* Get celcius value from average channel decoder */
	decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS_AVG, 0}, NULL,
			9, &celcius_from_avg_channel);

	/* Assert celcius temperature from manual average and average channel are equal */
	zassert_within(celcius_from_manual_avg, celcius_from_avg_channel,
		       AVG_TEMP_TOLERANCE);
}

/*
 * Test read and decode all three sensors.
 *
 * After the read call, manually read and convert the data from the test_buffer to
 * respective units and compare against the values returned by the decoder.
 */
ZTEST(pvt_tt_bh_tests, test_read_decode_all)
{
	float from_manual;
	float from_decoder;

	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	zassert_ok(ret, "Get decoder failed with %d", ret);

	pvt_tt_bh_delay_chain_set(1);

	ret = sensor_read(&test_all_iodev, &test_pvt_ctx, test_buf, sizeof(test_buf));
	zassert_ok(ret, "Sensor read failed with %d", ret);

	/* Test PD (Process Detector) - index 0 */
	const struct pvt_tt_bh_rtio_data *raw_freq =
		&(((const struct pvt_tt_bh_rtio_data *)test_buf)[0]);

	from_manual = pvt_tt_bh_raw_to_freq(raw_freq->raw);

	/* Get frequency value from decoder */
	decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD, 15}, NULL, 3,
			&from_decoder);

	/* Assert frequency from manual read and decoder are equal */
	zassert_equal(from_manual, from_decoder);

	/* Test VM (Voltage Monitor) - index 1 */
	const struct pvt_tt_bh_rtio_data *raw_volt =
		&(((const struct pvt_tt_bh_rtio_data *)test_buf)[1]);

	from_manual = pvt_tt_bh_raw_to_volt(raw_volt->raw);

	/* Get voltage value from decoder */
	decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM, 7}, NULL, 3,
			&from_decoder);

	/* Assert voltage from manual read and decoder are equal */
	zassert_equal(from_manual, from_decoder);

	/* Test TS (Temperature Sensor) - index 2 */
	const struct pvt_tt_bh_rtio_data *raw_temp =
		&(((const struct pvt_tt_bh_rtio_data *)test_buf)[2]);

	from_manual = pvt_tt_bh_raw_to_temp(raw_temp->raw);

	/* Get temperature value from decoder */
	decoder->decode(test_buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, 7}, NULL, 3,
			&from_decoder);

	/* Assert temperature from manual read and decoder are equal */
	zassert_equal(from_manual, from_decoder);
}

ZTEST_SUITE(pvt_tt_bh_tests, NULL, NULL, NULL, NULL, NULL);
