/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/tt_avs.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TT_AVS_DRV_NODE DT_NODELABEL(tenstorrent_avs)

static const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(tt_avs));

ZTEST(test_driver_tt_avs, test_read_write_voltage)
{
	uint16_t voltage_in_mV;
	int ret;

	zassert_true(device_is_ready(dev), "Device not ready");

	ret = avs_write_voltage(dev, 800, AVS_VCORE_RAIL);
	zassert_equal(ret, 0, "Error when writing voltage");

	ret = avs_read_voltage(dev, AVS_VCORE_RAIL, &voltage_in_mV);
	zassert_equal(ret, 0, "Error when reading voltage");
	// TODO: appropriate range
	zassert_true(voltage_in_mV >= 775 && voltage_in_mV <= 825,
		     "Readback voltage %dmV out of expected range", voltage_in_mV);
}

ZTEST(test_driver_tt_avs, test_read_write_vout_trans_rate)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_current)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_temp)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_force_voltage_reset)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_write_power_mode)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_write_status)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_version)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST(test_driver_tt_avs, test_read_system_input_current)
{
	// TODO
	zassert_equal(0, 0, "Dummy");
}

ZTEST_SUITE(test_driver_tt_avs, NULL, NULL, NULL, NULL, NULL);
