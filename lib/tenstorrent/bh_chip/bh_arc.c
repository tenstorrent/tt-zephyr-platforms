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
	int ret;

	ret = bharc_enable_i2cbus(dev);
	if (ret != 0) {
		bharc_disable_i2cbus(dev);
		return ret;
	}

	ret = smbus_block_read(dev->smbus.bus, dev->smbus.addr, cmd, count, output);

	int newret = bharc_disable_i2cbus(dev);

	if (ret == 0) {
		return newret;
	}

	return ret;
}

int bharc_smbus_block_write(const struct bh_arc *dev, uint8_t cmd, uint8_t count, uint8_t *input)
{
	int ret;

	ret = bharc_enable_i2cbus(dev);
	if (ret != 0) {
		bharc_disable_i2cbus(dev);
		return ret;
	}

	ret = smbus_block_write(dev->smbus.bus, dev->smbus.addr, cmd, count, input);

	int newret = bharc_disable_i2cbus(dev);

	if (ret == 0) {
		return newret;
	}

	return ret;
}

int bharc_smbus_word_data_write(const struct bh_arc *dev, uint16_t cmd, uint16_t word)
{
	int ret;

	ret = bharc_enable_i2cbus(dev);
	if (ret != 0) {
		bharc_disable_i2cbus(dev);
		return ret;
	}

	ret = smbus_word_data_write(dev->smbus.bus, dev->smbus.addr, cmd, word);

	int newret = bharc_disable_i2cbus(dev);

	if (ret == 0) {
		return newret;
	}

	return ret;
}
