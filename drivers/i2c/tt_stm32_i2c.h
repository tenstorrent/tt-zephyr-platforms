/*
 * Copyright (c) 2016 BayLibre, SAS
 * Copyright (c) 2017 Linaro Ltd
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef ZEPHYR_DRIVERS_I2C_TT_STM32_I2C_H_
#define ZEPHYR_DRIVERS_I2C_TT_STM32_I2C_H_

#include <tenstorrent/tt_stm32.h>
#include <zephyr/drivers/gpio.h>

typedef void (*irq_config_func_t)(const struct device *port);

/* TODO: @drosen @cfriedt HAX, please fix when possible */
/* #if DT_HAS_COMPAT_STATUS_OKAY(st_tt_stm32_i2c) */
struct tt_i2c_config_timing {
	/* i2c peripheral clock in Hz */
	/**
	 * @brief structure to convey optional i2c timings settings
	 */
	uint32_t periph_clock;
	/* i2c bus speed in Hz */
	uint32_t i2c_speed;
	/* I2C_TIMINGR register value of i2c v2 peripheral */
	uint32_t timing_setting;
};
/* #endif */
/* */

struct tt_stm32_i2c_config {
#ifdef CONFIG_TT_I2C_STM32_INTERRUPT
	irq_config_func_t irq_config_func;
#endif
#ifdef CONFIG_TT_I2C_STM32_SELECT_GPIOS
	struct gpio_dt_spec scl;
	struct gpio_dt_spec sda;
#endif /* CONFIG_I2C_STM32_BUS_RECOVERY */
	const struct stm32_pclken *pclken;
	size_t pclk_len;
	I2C_TypeDef *i2c;
	uint32_t bitrate;
	const struct pinctrl_dev_config *pcfg;
	const struct tt_i2c_config_timing *timings;
	size_t n_timings;
};

struct tt_i2c_bitbang_io {
	/* Set the state of the SCL line (zero/non-zero value) */
	int (*get_scl)(const struct tt_stm32_i2c_config *config);
	/* Set the state of the SCL line (zero/non-zero value) */
	void (*set_scl)(const struct tt_stm32_i2c_config *config, int state);
	/* Set the state of the SDA line (zero/non-zero value) */
	void (*set_sda)(const struct tt_stm32_i2c_config *config, int state);
	/* Return the state of the SDA line (zero/non-zero value) */
	int (*get_sda)(const struct tt_stm32_i2c_config *config);
};

struct tt_i2c_bitbang {
	struct tt_i2c_bitbang_io io;
	const struct tt_stm32_i2c_config *config;
	uint32_t delays[2];
	unsigned int *abort;
};

struct tt_stm32_i2c_data {
#ifdef CONFIG_TT_I2C_STM32_INTERRUPT
	struct k_sem device_sync_sem;
#endif

#ifdef CONFIG_TT_I2C_STM32_BYTE_POLL
	struct tt_i2c_bitbang ctx;
#endif
	struct k_sem bus_mutex;
	uint32_t dev_config;
	struct {
		unsigned int buffer_overflow;
		unsigned int is_slave_stop;
		unsigned int is_write;
		unsigned int is_arlo;
		unsigned int is_nack;
		unsigned int is_err;
		unsigned int *abort;
		struct i2c_msg *msg;
		unsigned int len;
		uint8_t *buf;
	} current;
	bool is_configured;
	bool smbalert_active;
	enum i2c_stm32_mode mode;
	unsigned int *abort;
#ifdef CONFIG_SMBUS_STM32_SMBALERT
	tt_stm32_i2c_smbalert_cb_func_t smbalert_cb_func;
	const struct device *smbalert_cb_dev;
#endif
};

int tt_stm32_i2c_enable(const struct device *dev);
void tt_stm32_i2c_disable(const struct device *dev);

int32_t tt_stm32_i2c_configure_timing(const struct device *dev, uint32_t clk);
int tt_stm32_i2c_runtime_configure(const struct device *dev, uint32_t config);
int tt_stm32_i2c_get_config(const struct device *dev, uint32_t *config);

void tt_stm32_i2c_event_isr(void *arg);
void tt_stm32_i2c_error_isr(void *arg);
#ifdef CONFIG_TT_I2C_STM32_COMBINED_INTERRUPT
void tt_stm32_i2c_combined_isr(void *arg);
#endif

#endif /* ZEPHYR_DRIVERS_I2C_TT_I2C_LL_STM32_H_ */
