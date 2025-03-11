/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/bh_chip.h>
#include <tenstorrent/fan_ctrl.h>

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

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);

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

	k_mutex_unlock(&chip->data.reset_lock);

	return output;
}

int bh_chip_set_static_info(struct bh_chip *chip, bmStaticInfo *info)
{
	int ret;

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);
	ret = bharc_smbus_block_write(&chip->config.arc, 0x20, sizeof(bmStaticInfo),
				      (uint8_t *)info);
	k_mutex_unlock(&chip->data.reset_lock);

	return ret;
}

int bh_chip_set_input_current(struct bh_chip *chip, int32_t *current)
{
	int ret;

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);
	ret = bharc_smbus_block_write(&chip->config.arc, 0x22, 4, (uint8_t *)current);
	k_mutex_unlock(&chip->data.reset_lock);

	return ret;
}
int bh_chip_set_fan_rpm(struct bh_chip *chip, uint16_t rpm)
{
	int ret;

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);
	ret = bharc_smbus_word_data_write(&chip->config.arc, 0x23, rpm);
	k_mutex_unlock(&chip->data.reset_lock);

	return ret;
}

int bh_chip_set_board_pwr_lim(struct bh_chip *chip, uint16_t max_pwr)
{
	int ret;

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);
	ret = bharc_smbus_word_data_write(&chip->config.arc, 0x24, max_pwr);
	k_mutex_unlock(&chip->data.reset_lock);

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

	/* Ramp up fan */
	if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
		set_fan_speed(100);
	}

	/* Assert ASIC reset */
	bh_chip_reset_chip(chip, true);
}

int therm_trip_gpio_setup(struct bh_chip *chip)
{
	/* Set up therm trip interrupt */
	int ret;

	ret = gpio_pin_configure_dt(&chip->config.therm_trip, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&chip->config.therm_trip, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(&chip->therm_trip_cb, therm_trip_detected,
			   BIT(chip->config.therm_trip.pin));
	ret = gpio_add_callback_dt(&chip->config.therm_trip, &chip->therm_trip_cb);

	return ret;
}

void pgood_fault_work_handler(struct k_work *work)
{
	struct bh_chip *chip = CONTAINER_OF(work, struct bh_chip, pgood_fault_worker);
	static const struct gpio_dt_spec board_fault_led =
		GPIO_DT_SPEC_GET_OR(DT_PATH(board_fault_led), gpios, {0});

	k_sem_reset(&chip->data.pgood_high_sem);
	/* Assert board fault */
	gpio_pin_set_dt(&board_fault_led, 1);
	/* Report over SMBus - to add later */
	/* Assert ASIC reset */
	bh_chip_assert_asic_reset(chip);
	/* Wait for PGOOD to rise */
	k_sem_take(&chip->data.pgood_high_sem, K_MSEC(50));
	/* Follow out of reset procedure */
	bh_chip_reset_chip(chip, true);
	/* Clear board fault */
	gpio_pin_set_dt(&board_fault_led, 0);
	k_msleep(1000);
	if (!gpio_pin_get_dt(&chip->config.pgood)) {
		/* Assert board fault */
		gpio_pin_set_dt(&board_fault_led, 1);
		/* Do not deassert ASIC reset until power cycle */
		bh_chip_assert_asic_reset(chip);
		/* Report more severe fault over IPMI - to add later */
	}
}

void pgood_fall_detected(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bh_chip *chip = CONTAINER_OF(cb, struct bh_chip, pgood_fall_cb);

	k_work_submit(&chip->pgood_fault_worker);
}

void pgood_rise_detected(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct bh_chip *chip = CONTAINER_OF(cb, struct bh_chip, pgood_rise_cb);

	k_sem_give(&chip->data.pgood_high_sem);
}

int pgood_gpio_setup(struct bh_chip *chip)
{
	/* Set up PGOOD interrupt */
	int ret;

	ret = gpio_pin_configure_dt(&chip->config.pgood, GPIO_INPUT);
	if (ret != 0) {
		return ret;
	}
	ret = gpio_pin_interrupt_configure_dt(&chip->config.pgood, GPIO_INT_EDGE_TO_INACTIVE);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(&chip->pgood_fall_cb, pgood_fall_detected, BIT(chip->config.pgood.pin));
	ret = gpio_add_callback_dt(&chip->config.pgood, &chip->pgood_fall_cb);

	ret = gpio_pin_interrupt_configure_dt(&chip->config.pgood, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		return ret;
	}
	gpio_init_callback(&chip->pgood_rise_cb, pgood_rise_detected, BIT(chip->config.pgood.pin));
	ret = gpio_add_callback_dt(&chip->config.pgood, &chip->pgood_fall_cb);

	return ret;
}
