/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C Driver for: STM32G0
 *
 */

#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <stm32_ll_i2c.h>
#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include "tt_stm32_i2c.h"
#include "stm32g0xx_ll_i2c.h"
#include "zephyr/sys_clock.h"
#include <zephyr/drivers/pinctrl.h>

#include "i2c_bitbang.h"
#include <zephyr/drivers/gpio.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tt_stm32_i2c_api);

#include "i2c-priv.h"

#define LOG_ERR_RATELIMIT(ms, ...)                                                                 \
	BUILD_ASSERT((ms) >= 0, "ms must be >= 0");                                                \
	do {                                                                                       \
		static uint32_t _last_ms;                                                          \
		uint32_t _now_ms = k_uptime_get_32();                                              \
                                                                                                   \
		if ((_now_ms - _last_ms) >= ms) {                                                  \
			LOG_ERR(__VA_ARGS__);                                                      \
			_last_ms = _now_ms;                                                        \
		}                                                                                  \
	} while (0)

int tt_stm32_i2c_configure_timing(const struct device *dev, uint32_t clock)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;
	uint32_t i2c_hold_time_min, i2c_setup_time_min;
	uint32_t i2c_h_min_time, i2c_l_min_time;
	uint32_t presc = 1U;
	uint32_t timing = 0U;

	/*  Look for an adequate preset timing value */
	for (uint32_t i = 0; i < cfg->n_timings; i++) {
		const struct tt_i2c_config_timing *preset = &cfg->timings[i];
		uint32_t speed = i2c_map_dt_bitrate(preset->i2c_speed);

		if ((I2C_SPEED_GET(speed) == I2C_SPEED_GET(data->dev_config)) &&
		    (preset->periph_clock == clock)) {
			/*  Found a matching periph clock and i2c speed */
			LL_I2C_SetTiming(i2c, preset->timing_setting);
			return 0;
		}
	}

	/* No preset timing was provided, let's dynamically configure */
	switch (I2C_SPEED_GET(data->dev_config)) {
	case I2C_SPEED_STANDARD:
		i2c_h_min_time = 4000U;
		i2c_l_min_time = 4700U;
		i2c_hold_time_min = 500U;
		i2c_setup_time_min = 1250U;
		break;
	case I2C_SPEED_FAST:
		i2c_h_min_time = 600U;
		i2c_l_min_time = 1300U;
		i2c_hold_time_min = 375U;
		i2c_setup_time_min = 500U;
		break;
	default:
		LOG_ERR("i2c: speed above \"fast\" requires manual timing configuration, "
			"see \"timings\" property of st,stm32-i2c-v2 devicetree binding");
		return -EINVAL;
	}

	/* Calculate period until prescaler matches */
	do {
		uint32_t t_presc = clock / presc;
		uint32_t ns_presc = NSEC_PER_SEC / t_presc;
		uint32_t sclh = i2c_h_min_time / ns_presc;
		uint32_t scll = i2c_l_min_time / ns_presc;
		uint32_t sdadel = i2c_hold_time_min / ns_presc;
		uint32_t scldel = i2c_setup_time_min / ns_presc;

		if ((sclh - 1) > 255 || (scll - 1) > 255) {
			++presc;
			continue;
		}

		if (sdadel > 15 || (scldel - 1) > 15) {
			++presc;
			continue;
		}

		timing =
			__LL_I2C_CONVERT_TIMINGS(presc - 1, scldel - 1, sdadel, sclh - 1, scll - 1);
		break;
	} while (presc < 16);

	if (presc >= 16U) {
		LOG_DBG("I2C:failed to find prescaler value");
		return -EINVAL;
	}

	LL_I2C_SetTiming(i2c, timing);

	return 0;
}

static int tt_stm32_i2c_bitbang_get_scl(const struct tt_stm32_i2c_config *config)
{
	return gpio_pin_get_dt(&config->scl) == 0 ? 0 : 1;
}

static void tt_stm32_i2c_bitbang_set_scl(const struct tt_stm32_i2c_config *config, int state)
{
	gpio_pin_set_dt(&config->scl, state);
}

static void tt_stm32_i2c_bitbang_set_sda(const struct tt_stm32_i2c_config *config, int state)
{
	gpio_pin_set_dt(&config->sda, state);
}

static int tt_stm32_i2c_bitbang_get_sda(const struct tt_stm32_i2c_config *config)
{
	return gpio_pin_get_dt(&config->sda) == 0 ? 0 : 1;
}

#define T_LOW    0
#define T_HIGH   1
#define T_SU_STA T_LOW
#define T_HD_STA T_HIGH
#define T_SU_STP T_HIGH
#define T_BUF    T_LOW

#define NS_TO_SYS_CLOCK_HW_CYCLES(ns)                                                              \
	((uint64_t)sys_clock_hw_cycles_per_sec() * (ns) / NSEC_PER_SEC + 1)

int tt_i2c_bitbang_configure(struct tt_i2c_bitbang *context, uint32_t dev_config)
{
	/* Check for features we don't support */
	if (I2C_ADDR_10_BITS & dev_config) {
		return -ENOTSUP;
	}

	/* Setup speed to use */
	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		context->delays[T_LOW] = NS_TO_SYS_CLOCK_HW_CYCLES(4700);
		context->delays[T_HIGH] = NS_TO_SYS_CLOCK_HW_CYCLES(4000);
		break;
	case I2C_SPEED_FAST:
		context->delays[T_LOW] = NS_TO_SYS_CLOCK_HW_CYCLES(1300);
		context->delays[T_HIGH] = NS_TO_SYS_CLOCK_HW_CYCLES(600);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

void tt_i2c_bitbang_init(struct tt_i2c_bitbang *context, const struct tt_i2c_bitbang_io io)
{
	context->io = io;
	tt_i2c_bitbang_configure(context, I2C_SPEED_STANDARD << I2C_SPEED_SHIFT);
}

void i2c_set_scl(struct tt_i2c_bitbang *context, int state)
{
	context->io.set_scl(context->config, state);
}

int i2c_get_scl(struct tt_i2c_bitbang *context)
{
	return context->io.get_scl(context->config);
}

void i2c_set_sda(struct tt_i2c_bitbang *context, int state)
{
	context->io.set_sda(context->config, state);
}

int i2c_get_sda(struct tt_i2c_bitbang *context)
{
	return context->io.get_sda(context->config);
}

void i2c_delay(unsigned int cycles_to_wait)
{
	uint32_t start = k_cycle_get_32();

	/* Wait until the given number of cycles have passed */
	while (k_cycle_get_32() - start < cycles_to_wait) {
	}
}

int i2c_scl_high(struct tt_i2c_bitbang *context)
{
	/* Infinite loop... make sure we can abort in the case of timeout or the abort flag. */
	/* k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT)); */
	i2c_set_scl(context, 1);
	do {
		i2c_delay(context->delays[T_HIGH]);

		k_yield();

		if (context->abort != NULL && *context->abort) {
			return -ECANCELED;
		}

		/* What if we never timed out
		 *if (sys_timepoint_expired(timeout)) {
		 *  LOG_ERR("TIMEOUT Waiting for clock to stop stretching");
		 *  return -ETIMEDOUT;
		 *}
		 */
	} while (i2c_get_scl(context) == 0);

	return 0;
}

int i2c_start(struct tt_i2c_bitbang *context)
{
	int ret;

	if (!i2c_get_sda(context)) {
		/*
		 * SDA is already low, so we need to do something to make it
		 * high. Try pulsing clock low to get slave to release SDA.byte_poll
		 */
		i2c_set_scl(context, 0);
		i2c_delay(context->delays[T_LOW]);
		/* NOTE(drosen): I don't need to handle clock stretching here... but I'm feeling
		 * paranoid today
		 */
		ret = i2c_scl_high(context);
		if (ret) {
			return ret;
		}
		i2c_delay(context->delays[T_SU_STA]);
	}
	i2c_set_sda(context, 0);
	i2c_delay(context->delays[T_HD_STA]);

	i2c_set_scl(context, 0);
	i2c_delay(context->delays[T_LOW]);

	return 0;
}

int i2c_repeated_start(struct tt_i2c_bitbang *context)
{
	int ret;

	i2c_set_sda(context, 1);
	/* NOTE(drosen): I don't need to handle clock stretching here... but I'm feeling paranoid
	 * today
	 */
	ret = i2c_scl_high(context);
	if (ret) {
		return ret;
	}

	i2c_delay(context->delays[T_SU_STA]);
	ret = i2c_start(context);
	if (ret) {
		return ret;
	}

	return 0;
}

int i2c_stop(struct tt_i2c_bitbang *context)
{
	int ret;

	i2c_set_sda(context, 0);
	i2c_delay(context->delays[T_LOW]);

	/* NOTE(drosen): I don't need to handle clock stretching here... but I'm feeling paranoid
	 * today
	 */
	ret = i2c_scl_high(context);
	if (ret) {
		return ret;
	}

	i2c_delay(context->delays[T_SU_STP]);
	i2c_set_sda(context, 1);
	i2c_delay(context->delays[T_BUF]); /* In case we start again too soon */

	return 0;
}

int i2c_write_bit(struct tt_i2c_bitbang *context, int bit)
{
	int ret;

	/* SDA hold time is zero, so no need for a delay here */
	i2c_set_sda(context, bit);
	ret = i2c_scl_high(context);
	if (ret) {
		return ret;
	}
	i2c_set_scl(context, 0);
	i2c_delay(context->delays[T_LOW]);

	return 0;
}

/*
 * Returns the bit as read from SDA or an error
 *
 * @return a positive number (or zero) to be interpreted as a boolean value.
 * If nevative it should be treated as an error.
 */
int i2c_read_bit(struct tt_i2c_bitbang *context)
{
	bool bit;
	int ret;

	/* SDA hold time is zero, so no need for a delay here */
	i2c_set_sda(context, 1); /* Stop driving low, so slave has control */

	ret = i2c_scl_high(context);
	if (ret) {
		return ret;
	}

	bit = i2c_get_sda(context);

	i2c_set_scl(context, 0);
	i2c_delay(context->delays[T_LOW]);
	return bit;
}

/*
 * Returns if the byte as ACKd or NACKd
 *
 * @return a positive number (or zero) to be interpreted as a boolean value.
 * 'true' for ACK, 'false' for NACk.
 * If nevative it should be treated as an error.
 */
int i2c_write_byte(struct tt_i2c_bitbang *context, uint8_t byte)
{
	uint8_t mask = 1 << 7;
	int ret;

	do {
		ret = i2c_write_bit(context, byte & mask);
		if (ret) {
			return ret;
		}
	} while (mask >>= 1);

	/* Return inverted ACK bit, i.e. 'true' for ACK, 'false' for NACK */
	return !i2c_read_bit(context);
}

/*
 * Returns the byte read from the i2c bus
 *
 * @return a positive number (or zero) to be interpreted as a u8.
 * If nevative it should be treated as an error.
 */
int i2c_read_byte(struct tt_i2c_bitbang *context)
{
	unsigned int byte = 1U;

	do {
		byte <<= 1;
		int value = i2c_read_bit(context);

		if (value < 0) {
			return value;
		}
		byte |= (uint8_t)value;
	} while (!(byte & (1 << 8)));

	return byte;
}

int tt_stm32_i2c_enable(const struct device *dev)
{
	const struct tt_stm32_i2c_config *config = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	struct tt_i2c_bitbang bitbang_ctx;
	struct tt_i2c_bitbang_io bitbang_io = {
		.get_scl = tt_stm32_i2c_bitbang_get_scl,
		.set_scl = tt_stm32_i2c_bitbang_set_scl,
		.set_sda = tt_stm32_i2c_bitbang_set_sda,
		.get_sda = tt_stm32_i2c_bitbang_get_sda,
	};
	bitbang_ctx.abort = data->abort;
	uint32_t bitrate_cfg;
	int error = 0;

	I2C_TypeDef *i2c = config->i2c;

	LL_I2C_Disable(i2c);

	if (!gpio_is_ready_dt(&config->scl)) {
		LOG_ERR("SCL GPIO device not ready");
		return -EIO;
	}

	if (!gpio_is_ready_dt(&config->sda)) {
		LOG_ERR("SDA GPIO device not ready");
		return -EIO;
	}

	error = gpio_pin_configure_dt(&config->scl, GPIO_OUTPUT_HIGH);
	if (error != 0) {
		LOG_ERR("failed to configure SCL GPIO (err %d)", error);
		return error;
	}

	error = gpio_pin_configure_dt(&config->sda, GPIO_INPUT | GPIO_OUTPUT_HIGH);
	if (error != 0) {
		LOG_ERR("failed to configure SDA GPIO (err %d)", error);
		return error;
	}

	tt_i2c_bitbang_init(&bitbang_ctx, bitbang_io);

	bitrate_cfg = i2c_map_dt_bitrate(config->bitrate) | I2C_MODE_CONTROLLER;
	error = tt_i2c_bitbang_configure(&bitbang_ctx, bitrate_cfg);
	if (error != 0) {
		LOG_ERR("failed to configure I2C bitbang (err %d)", error);
	}

	if (error == 0) {
		data->ctx = bitbang_ctx;
	}

	return error;
}

void tt_stm32_i2c_disable(const struct device *dev)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	(void)pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
}

int tt_stm32_i2c_send_message(const struct device *dev, uint16_t slave, struct i2c_msg msg,
			      bool start, bool cont)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	struct tt_i2c_bitbang *context = &data->ctx;

	context->config = cfg;

	uint8_t *buf, *buf_end;
	int ret = -EIO;
	bool expected_error = false;

	/* Escape hatch in case a reboot comes through */
	if (data->current.abort != NULL && *data->current.abort) {
		ret = -ECANCELED;
		goto finish;
	}

	if (msg.flags & I2C_MSG_RESTART) {
		int start_ret;

		if (start) {
			/* Make sure we're in a good state so slave recognises the Start */
			start_ret = i2c_scl_high(context);
			if (start_ret) {
				ret = start_ret;
				goto finish;
			}
			start_ret = i2c_start(context);
		} else {
			start_ret = i2c_repeated_start(context);
		}

		/* Start failed, go to finish... */
		/* NOTE(drosen): I might want to look into what bus reset condition would be
		 * appropriate for this failure.
		 */
		if (start_ret) {
			ret = start_ret;
			goto finish;
		}

		/* Send address after any Start condition */
		unsigned int byte0 = slave << 1;

		byte0 |= (msg.flags & I2C_MSG_RW_MASK) == I2C_MSG_READ;
		int ack = i2c_write_byte(context, byte0);

		if (ack < 0) {
			ret = ack;
			goto finish;
		} else if (!ack) {
			/* If we don't get an ack when starting it's probably safe to assume that
			 * the endpoint isn't
			 */
			/* on the bus. Not getting an ACK during a restart is probably a bigger
			 * problem...
			 */
			if (!start) {
				LOG_ERR("No ACK received while writing addr");
			} else {
				/* Easy way of not printing too often */
				expected_error = true;
			}
			goto finish; /* No ACK received */
		}
	}

	/* Transfer data */
	buf = msg.buf;
	buf_end = buf + msg.len;
	if ((msg.flags & I2C_MSG_RW_MASK) == I2C_MSG_READ) {
		/* Read */
		while (buf < buf_end) {
			int byte = i2c_read_byte(context);

			if (byte < 0) {
				ret = byte;
				goto finish;
			}
			*buf++ = (uint8_t)byte;
			/* I want to support writing multiple messages back-to-back without a
			 * restart.
			 */
			/* So I only want to send the NACK on the final continue message. */
			int rc = i2c_write_bit(context, (buf == buf_end) && !cont);

			if (rc) {
				ret = rc;
				goto finish;
			}
		}
	} else {
		/* Write */
		while (buf < buf_end) {
			int ack = i2c_write_byte(context, *buf++);

			if (ack < 0) {
				ret = ack;
				goto finish;
			} else if (!ack) {
				LOG_ERR("No ACK received while writing buffer");
				goto finish; /* No ACK received */
			}
		}
	}

	ret = 0;

	/* Issue stop condition if necessary */
	if (msg.flags & I2C_MSG_STOP) {
finish:
		int stop_ret = i2c_stop(context);

		if (ret == 0) {
			/* If stop failed when everything else succeeded that's our error. */
			ret = stop_ret;
		}
	}

	if (ret != 0) {
		if (expected_error) {
			LOG_ERR_RATELIMIT(5000, "I2C MSG Failed On Start  with %d", ret);
		} else {
			LOG_ERR_RATELIMIT(5000, "I2C MSG Failed with %d", ret);
		}
	}

	return ret;
}
