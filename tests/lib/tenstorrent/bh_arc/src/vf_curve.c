/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include <stdlib.h>

ZTEST(vf_curve, test_get_freq_curve_from_voltage_handler)
{
	union request req = {0};
	struct response rsp = {0};

	/* Test with voltage 800mV -> should get frequency */
	req.data[0] = TT_SMC_MSG_GET_FREQ_CURVE_FROM_VOLTAGE;
	req.data[1] = 800; /* voltage in mV */
	msgqueue_request_push(0, &req);

	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	/* Verify we get a reasonable frequency value */
	zassert_true(rsp.data[1] > 0, "Expected positive frequency");
	zassert_true(rsp.data[1] < 5000, "Expected reasonable frequency range");
}

ZTEST(vf_curve, test_get_voltage_curve_from_freq_handler)
{
	union request req = {0};
	struct response rsp = {0};

	/* Test with frequency 1000MHz -> should get voltage */
	req.data[0] = TT_SMC_MSG_GET_VOLTAGE_CURVE_FROM_FREQ;
	req.data[1] = 1000; /* frequency in MHz */
	msgqueue_request_push(0, &req);

	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	/* Verify we get a reasonable voltage value */
	zassert_true(rsp.data[1] > 500, "Expected voltage > 500mV");
	zassert_true(rsp.data[1] < 1200, "Expected voltage < 1200mV");
}

ZTEST(vf_curve, test_voltage_frequency_roundtrip)
{
	union request req = {0};
	struct response rsp = {0};
	uint32_t original_freq = 1000; /* MHz */
	uint32_t calculated_voltage;
	uint32_t calculated_freq;

	/* First: Convert frequency to voltage */
	req.data[0] = TT_SMC_MSG_GET_VOLTAGE_CURVE_FROM_FREQ;
	req.data[1] = original_freq;

	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	calculated_voltage = rsp.data[1];

	/* Second: Convert voltage back to frequency */
	req.data[0] = TT_SMC_MSG_GET_FREQ_CURVE_FROM_VOLTAGE;
	req.data[1] = calculated_voltage;

	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	calculated_freq = rsp.data[1];

	/* Verify roundtrip accuracy (allow small error due to integer conversion) */
	int32_t freq_diff = (int32_t)calculated_freq - (int32_t)original_freq;

	zassert_true(abs(freq_diff) < 50, "Roundtrip frequency error too large: %d", freq_diff);
}

ZTEST_SUITE(vf_curve, NULL, NULL, NULL, NULL, NULL);
