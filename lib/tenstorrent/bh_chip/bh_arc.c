/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/bh_arc.h>

int bharc_smbus_block_read(const struct bh_arc *dev, uint8_t cmd, uint8_t *count, uint8_t *output)
{
	int ret;

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 1);
	}

	ret = smbus_block_read(dev->smbus.bus, dev->smbus.addr, cmd, count, output);

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 0);
	}

	return ret;
}

int bharc_smbus_block_write(const struct bh_arc *dev, uint8_t cmd, uint8_t count, uint8_t *input)
{
	int ret;

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 1);
	}

	ret = smbus_block_write(dev->smbus.bus, dev->smbus.addr, cmd, count, input);

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 0);
	}

	return ret;
}

int bharc_smbus_word_data_write(const struct bh_arc *dev, uint16_t cmd, uint16_t word)
{
	int ret;

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 1);
	}

	ret = smbus_word_data_write(dev->smbus.bus, dev->smbus.addr, cmd, word);

	if (dev->enable.port != NULL) {
		gpio_pin_set_dt(&dev->enable, 0);
	}

	return ret;
}
