/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i3c.h>
#include <zephyr/ztest.h>
#include <tenstorrent/occp.h>

#include <zephyr/drivers/misc/tt_smc_remoteproc.h>

#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

const struct device *i3c_dev = DEVICE_DT_GET(DT_PROP(ZEPHYR_USER_NODE, i3c_dev));
const struct gpio_dt_spec boot_gpio = GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, boot_gpios);
struct i3c_device_desc *remote_smc_dev;
struct occp_backend_i3c occp_backend;

bool occp_before(const void *unused)
{
	int ret;
	struct i3c_device_desc *dev_desc;

	ARG_UNUSED(unused);
	zassert_true(device_is_ready(i3c_dev), "I3C device not ready");
	/* Wait for BOOT GPIO to go high */
	zassert_true(device_is_ready(boot_gpio.port), "BOOT GPIO port not ready");

	gpio_pin_configure_dt(&boot_gpio, GPIO_INPUT);
	while (gpio_pin_get_dt(&boot_gpio) == 0) {
		/* wait */
	}
	ret = i3c_do_daa(i3c_dev);
	zassert_equal(ret, 0, "I3C dynamic address assignment failed: %d", ret);
	/* Find the I3C device */
	I3C_BUS_FOR_EACH_I3CDEV(i3c_dev, dev_desc) {
		if (dev_desc->pid == ((uint64_t)DT_PROP(ZEPHYR_USER_NODE, i3c_pid_high) << 32 |
				      DT_PROP(ZEPHYR_USER_NODE, i3c_pid_low))) {
			TC_PRINT("Found remote_device with PID 0x%012" PRIx64 "\n", dev_desc->pid);
			remote_smc_dev = dev_desc;
			break;
		}
	}
	zassert_not_null(remote_smc_dev, "Failed to get remote SMC device descriptor");
	ret = occp_backend_i3c_init(&occp_backend, remote_smc_dev);
	zassert_equal(ret, 0, "Failed to initialize OCCP I3C backend: %d", ret);

	TC_PRINT("Remote SMC is ready, proceeding with tests\n");
	return true;
}

ZTEST(occp, test_version)
{
	uint8_t major, minor, patch;
	int ret;

	ret = occp_get_version(&occp_backend.base, &major, &minor, &patch);
	zassert_equal(ret, 0, "Failed to get OCCP version: %d", ret);
	zassert_equal(major, 1, "Unexpected OCCP major version: %d", major);
	zassert_equal(minor, 0, "Unexpected OCCP minor version: %d", minor);
	zassert_equal(patch, 0, "Unexpected OCCP patch version: %d", patch);
}

ZTEST(occp, test_read_write)
{
	int ret;
	uint8_t write_data[4] = {0xde, 0xad, 0xbe, 0xef};
	uint8_t read_data[4] = {0};

	/* Write to the start of the SRAM space in SMC we are permitted to use */
	ret = occp_write_data(&occp_backend.base, 0xc0066000, write_data, sizeof(write_data));
	zassert_equal(ret, 0, "Failed to write data via OCCP: %d", ret);

	ret = occp_read_data(&occp_backend.base, 0xc0066000, read_data, sizeof(read_data));
	zassert_equal(ret, 0, "Failed to read data via OCCP: %d", ret);
	zassert_mem_equal(write_data, read_data, sizeof(write_data),
			  "Read data does not match written data");
}

ZTEST_SUITE(occp, occp_before, NULL, NULL, NULL, NULL);
