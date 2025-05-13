/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/ztest.h>

#include <tenstorrent/bh_chip.h>

static struct bh_chip test_chip = {
	.config = {
		.asic_reset = GPIO_DT_SPEC_GET(DT_PATH(asic_reset), gpios),
		.pgood = GPIO_DT_SPEC_GET(DT_PATH(pgood), gpios),
	}};

const struct device *gpio_emul = DEVICE_DT_GET(DT_NODELABEL(gpio0));
static const struct gpio_dt_spec board_fault_led =
	GPIO_DT_SPEC_GET(DT_PATH(board_fault_led), gpios);

ZTEST(pgood, test_pgood)
{
	/* Start with PGOOD high */
	gpio_emul_input_set(gpio_emul, 1, 1);
	/* Manually clear pgood_rise_triggered */
	test_chip.data.pgood_rise_triggered = 0;

	/* Wait 1ms so pgood_last_trip_ms will not be set to 0 */
	k_msleep(1);

	/* Set PGOOD low */
	gpio_emul_input_set(gpio_emul, 1, 0);
	/* Check that PGOOD fall was triggered */
	zassert_true(test_chip.data.pgood_fall_triggered);

	handle_pgood_event(&test_chip, board_fault_led);
	/* Check that PGOOD fall was handled */
	zassert_true(test_chip.data.pgood_last_trip_ms > 0);
	zassert_false(test_chip.data.pgood_fall_triggered);
	zassert_false(test_chip.data.pgood_severe_fault);

	/* Set PGOOD high */
	gpio_emul_input_set(gpio_emul, 1, 1);
	/* Check that PGOOD rise was triggered */
	zassert_true(test_chip.data.pgood_rise_triggered);
	/* Manually clear it because bh_chip_reset_chip can't run here */
	test_chip.data.pgood_rise_triggered = 0;

	/* Set PGOOD low */
	gpio_emul_input_set(gpio_emul, 1, 0);
	zassert_true(test_chip.data.pgood_fall_triggered);

	handle_pgood_event(&test_chip, board_fault_led);
	/* Check that PGOOD fall was handled and severe state was entered */
	zassert_true(test_chip.data.pgood_last_trip_ms > 0);
	zassert_false(test_chip.data.pgood_fall_triggered);
	zassert_true(test_chip.data.pgood_severe_fault);
}

static void before(void *arg)
{
	ARG_UNUSED(arg);

	zassert_ok(pgood_gpio_setup(&test_chip));
}

ZTEST_SUITE(pgood, NULL, NULL, before, NULL, NULL);
