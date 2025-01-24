/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/bh_chip.h>

#include <zephyr/kernel.h>

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

	k_mutex_lock(&chip->data.reset_lock, K_FOREVER);

	output.ret =
		bharc_smbus_block_read(&chip->config.arc, 0x10, &count, (uint8_t *)&output.msg);

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
