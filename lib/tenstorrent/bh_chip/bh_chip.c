/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/bh_chip.h>
#include <tenstorrent/fan_ctrl.h>
#include <tenstorrent/bm_event.h>

#include <zephyr/kernel.h>
#include <string.h>

void bh_chip_cancel_bus_transfer_set(struct bh_chip *dev)
{
	dev->data.bus_cancel_flag = 1;
}

void bh_chip_cancel_bus_transfer_clear(struct bh_chip *dev)
{
	dev->data.bus_cancel_flag = 0;
}

cm2bmMessageRet bh_chip_get_cm2bm_message(struct bh_chip *chip)
{
	cm2bmMessageRet output = {
		.ret = -1,
		.ack_ret = -1,
	};
	uint8_t count = sizeof(output.msg);
	uint8_t buf[32]; /* Max block counter per API */

	output.ret = bharc_smbus_block_read(&chip->config.arc, 0x10, &count, buf);
	memcpy(&output.msg, buf, sizeof(output.msg));

	if (output.ret == 0 && output.msg.msg_id != 0) {
		cm2bmAck ack = {0};

		ack.msg_id = output.msg.msg_id;
		ack.seq_num = output.msg.seq_num;
		union cm2bmAckWire wire_ack;

		wire_ack.f = ack;
		output.ack = ack;
		output.ack_ret = bharc_smbus_word_data_write(&chip->config.arc, 0x11, wire_ack.val);
	}

	return output;
}

int bh_chip_set_static_info(struct bh_chip *chip, bmStaticInfo *info)
{
	int ret;

	ret = bharc_smbus_block_write(&chip->config.arc, 0x20, sizeof(bmStaticInfo),
				      (uint8_t *)info);

	return ret;
}

int bh_chip_set_input_current(struct bh_chip *chip, int32_t *current)
{
	int ret;

	ret = bharc_smbus_block_write(&chip->config.arc, 0x22, 4, (uint8_t *)current);

	return ret;
}
int bh_chip_set_fan_rpm(struct bh_chip *chip, uint16_t rpm)
{
	int ret;

	ret = bharc_smbus_word_data_write(&chip->config.arc, 0x23, rpm);

	return ret;
}

int bh_chip_set_board_pwr_lim(struct bh_chip *chip, uint16_t max_pwr)
{
	int ret;

	ret = bharc_smbus_word_data_write(&chip->config.arc, 0x24, max_pwr);

	return ret;
}

void bh_chip_assert_asic_reset(const struct bh_chip *chip)
{
	gpio_pin_set_dt(&chip->config.asic_reset, 1);
}

void bh_chip_deassert_asic_reset(const struct bh_chip *chip)
{
	gpio_pin_set_dt(&chip->config.asic_reset, 0);
}

void bh_chip_assert_spi_reset(const struct bh_chip *chip)
{
	gpio_pin_set_dt(&chip->config.spi_reset, 1);
}

void bh_chip_deassert_spi_reset(const struct bh_chip *chip)
{
	gpio_pin_set_dt(&chip->config.spi_reset, 0);
}

int bh_chip_reset_chip(struct bh_chip *chip, bool force_reset)
{
	return jtag_bootrom_reset_sequence(chip, force_reset);
}

void therm_trip_detected(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bh_chip *chip = CONTAINER_OF(cb, struct bh_chip, therm_trip_cb);

	chip->data.therm_trip_triggered = true;
	bh_chip_cancel_bus_transfer_set(chip);
	bm_event_post(WAKE_BM_MAIN_LOOP);
}

int therm_trip_gpio_setup(struct bh_chip *chip)
{
	/* Set up therm trip interrupt */
	int ret;

	ret = gpio_pin_configure_dt(&chip->config.therm_trip, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(&chip->therm_trip_cb, therm_trip_detected,
			   BIT(chip->config.therm_trip.pin));
	ret = gpio_add_callback_dt(&chip->config.therm_trip, &chip->therm_trip_cb);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&chip->config.therm_trip, GPIO_INT_EDGE_TO_ACTIVE);

	return ret;
}

void pgood_change_detected(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bh_chip *chip = CONTAINER_OF(cb, struct bh_chip, pgood_cb);

	/* Sample PGOOD to see if it rose or fell */
	/* TODO: is this reliable enough? */
	if (gpio_pin_get_dt(&chip->config.pgood)) {
		chip->data.pgood_rise_triggered = true;
	} else {
		chip->data.pgood_fall_triggered = true;
	}
	bm_event_post(WAKE_BM_MAIN_LOOP);
}

int pgood_gpio_setup(struct bh_chip *chip)
{
	/* Set up PGOOD interrupt */
	int ret;

	ret = gpio_pin_configure_dt(&chip->config.pgood, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(&chip->pgood_cb, pgood_change_detected, BIT(chip->config.pgood.pin));
	ret = gpio_add_callback_dt(&chip->config.pgood, &chip->pgood_cb);
	if (ret != 0) {
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&chip->config.pgood, GPIO_INT_EDGE_BOTH);

	return ret;
}
