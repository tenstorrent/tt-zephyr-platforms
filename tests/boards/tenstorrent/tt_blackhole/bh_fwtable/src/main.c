/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>

/* This test is designed to verify that the driver instance, enabled via an
 * external devicetree overlay, is found and properly initialized by the kernel.
 */
ZTEST(bh_fwtable_validation_suite, test_driver_enabled_and_ready)
{
	/*
	 * DEVICE_DT_GET will find the device structure associated with the
	 * 'fwtable' node label from the final, merged devicetree.
	 */
	const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

	/*
	 * First, check if the device was found at all. This confirms the
	 * devicetree node was present.
	 */
	zassert_not_null(fwtable_dev, "TEST FAILED: fwtable device was not found.");

	/*
	 * Second, check if the device is ready. This confirms that its init
	 * function was called by the kernel and returned successfully. This is
	 * the ultimate proof that status="okay" and the Kconfigs worked.
	 */
	zassert_true(device_is_ready(fwtable_dev), "TEST FAILED: fwtable device is not ready.");
}

/* Define and run the test suite */
ZTEST_SUITE(bh_fwtable_validation_suite, NULL, NULL, NULL, NULL, NULL);
