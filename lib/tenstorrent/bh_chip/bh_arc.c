/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/drivers/gpio.h"
#include <tenstorrent/bh_arc.h>

int bharc_enable_i2cbus(const struct bh_arc *dev)
{
	int ret = 0;

	if (dev->enable.port != NULL) {
		ret = gpio_pin_configure_dt(&dev->enable, GPIO_OUTPUT_ACTIVE);
	}

	return ret;
}

int bharc_disable_i2cbus(const struct bh_arc *dev)
{
	int ret = 0;

	if (dev->enable.port != NULL) {
		ret = gpio_pin_configure_dt(&dev->enable, GPIO_OUTPUT_INACTIVE);
	}

	return ret;
}

int bharc_smbus_block_read(const struct bh_arc *dev, uint8_t cmd, uint8_t *count, uint8_t *output)
{
	return smbus_block_read(dev->smbus.bus, dev->smbus.addr, cmd, count, output);
}

int bharc_smbus_block_write(const struct bh_arc *dev, uint8_t cmd, uint8_t count, uint8_t *input)
{
	return smbus_block_write(dev->smbus.bus, dev->smbus.addr, cmd, count, input);
}

int bharc_smbus_block_write_block_read(const struct bh_arc *dev, uint8_t cmd, uint8_t snd_count,
				       uint8_t *send_buf, uint8_t *rcv_count, uint8_t *rcv_buf)
{
	return smbus_block_pcall(dev->smbus.bus, dev->smbus.addr, cmd, snd_count, send_buf,
				 rcv_count, rcv_buf);
}

int bharc_smbus_word_data_write(const struct bh_arc *dev, uint16_t cmd, uint16_t word)
{
	return smbus_word_data_write(dev->smbus.bus, dev->smbus.addr, cmd, word);
}

int bharc_smbus_word_data_read(const struct bh_arc *dev, uint16_t cmd, uint16_t *word)
{
	return smbus_word_data_read(dev->smbus.bus, dev->smbus.addr, cmd, word);
}

int bharc_smbus_byte_data_write(const struct bh_arc *dev, uint8_t cmd, uint8_t word)
{
	return smbus_byte_data_write(dev->smbus.bus, dev->smbus.addr, cmd, word);
}
