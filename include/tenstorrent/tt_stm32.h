/*
 * Copyright (c) 2023 SILA Embedded Solutions GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_I2C_STM32_H_
#define ZEPHYR_INCLUDE_DRIVERS_I2C_STM32_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

enum i2c_stm32_mode {
	I2CSTM32MODE_I2C,
	I2CSTM32MODE_SMBUSHOST,
	I2CSTM32MODE_SMBUSDEVICE,
	I2CSTM32MODE_SMBUSDEVICEARP,
};

void tt_stm32_i2c_set_abort_ptr(const struct device *dev, unsigned int *abort);
void tt_stm32_i2c_set_smbus_mode(const struct device *dev, enum i2c_stm32_mode mode);

/* Raw i2c transfer function (be very careful you MUST call start and stop to ensure you don't */
/* deadlock your bus) */
void tt_stm32_i2c_start_transfer(const struct device *dev);
int tt_stm32_i2c_send_message(const struct device *dev, uint16_t slave, struct i2c_msg msg,
			      bool start, bool cont);
void tt_stm32_i2c_stop_transfer(const struct device *dev);

#ifdef CONFIG_SMBUS_STM32_SMBALERT
typedef void (*tt_stm32_i2c_smbalert_cb_func_t)(const struct device *dev);

void tt_stm32_i2c_smbalert_set_callback(const struct device *dev, i2c_stm32_smbalert_cb_func_t func,
					const struct device *cb_dev);
void tt_stm32_i2c_smbalert_enable(const struct device *dev);
void tt_stm32_i2c_smbalert_disable(const struct device *dev);
#endif /* CONFIG_SMBUS_STM32_SMBALERT */

#endif /* ZEPHYR_INCLUDE_DRIVERS_I2C_STM32_H_ */
