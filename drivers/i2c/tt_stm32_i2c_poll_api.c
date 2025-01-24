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

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tt_stm32_i2c_api);

#include "i2c-priv.h"

#define STM32_I2C_MAX_TRANSFER_SIZE (0xFF)

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

/* StateMachine */
/* During sending */
/*  1.TXIS flag is set after each byte transmission, after the 9th SCL pulse when the ACK is */
/*  received. */
/*    The flag is cleared when I@C-TXDR register is written with the next byte to be transferred */
/*    - NOTE: TXIE bit must be set in the I2C_CR1 REG. */
/*  2. Things get more complicated when we are sending more than 255 bytes (or want to make our */
/*  lives harder). Then this happens we need reload mode. (Only enabled if the corresponding bit is
 */
/*  set in the I2C_CR2 register). If we have it set, then when we exceed our NBYTES transfer */
/*  limit.... */
/*    1. TCR is set and the SCL line is set low until we have written a new non-zero value to NBYTES
 */
/*  3. If a NACK is received */
/*    - Then if RELOAD=0; */

static inline int check_errors(const struct device *dev, const char *funcname)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	I2C_TypeDef *i2c = cfg->i2c;

	if (LL_I2C_IsActiveFlag_NACK(i2c)) {
		LL_I2C_ClearFlag_NACK(i2c);
		LOG_DBG("%s: NACK", funcname);
		goto error;
	}

	if (LL_I2C_IsActiveFlag_ARLO(i2c)) {
		LL_I2C_ClearFlag_ARLO(i2c);
		LOG_DBG("%s: ARLO", funcname);
		goto error;
	}

	if (LL_I2C_IsActiveFlag_OVR(i2c)) {
		LL_I2C_ClearFlag_OVR(i2c);
		LOG_DBG("%s: OVR", funcname);
		goto error;
	}

	if (LL_I2C_IsActiveFlag_BERR(i2c)) {
		LL_I2C_ClearFlag_BERR(i2c);
		LOG_DBG("%s: BERR", funcname);
		goto error;
	}

	return 0;
error:
	if (LL_I2C_IsEnabledReloadMode(i2c)) {
		LL_I2C_DisableReloadMode(i2c);
	}
	return -EIO;
}

static inline int msg_abort(const struct device *dev)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_GenerateStopCondition(i2c);
	while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
	}

	LL_I2C_ClearFlag_STOP(i2c);
	LL_I2C_DisableReloadMode(i2c);

	return 0;
}

int i2c_shutdown(const struct device *dev)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_DisableReloadMode(i2c);
	LL_I2C_Disable(i2c);

	/* Infinite loop... make sure we can abort in the case of timeout or the abort flag. */
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT));

	while (LL_I2C_IsEnabled(i2c)) {
		if (data->current.abort != NULL && *data->current.abort) {
			return -ECANCELED;
		}

		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("shutdown: TIMEOUT");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

int tt_stm32_reset_i2c(const struct device *dev)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	I2C_TypeDef *i2c = cfg->i2c;

	int ret = i2c_shutdown(dev);

	if (ret == 0) {
		LL_I2C_Enable(i2c);
	}
	return ret;
}

static void tt_stm32_i2c_msg_setup(const struct device *dev, uint16_t slave, bool write)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	if (I2C_ADDR_10_BITS & data->dev_config) {
		LL_I2C_SetMasterAddressingMode(i2c, LL_I2C_ADDRESSING_MODE_10BIT);
		LL_I2C_SetSlaveAddr(i2c, (uint32_t)slave);

		/* Also need to configure HEAD10R here (leaving out for now) */
		/* but this indicates in the case of a 10 bit address read if the complete address
		 */
		/* sequence needs to be set. */
	} else {
		LL_I2C_SetMasterAddressingMode(i2c, LL_I2C_ADDRESSING_MODE_7BIT);
		LL_I2C_SetSlaveAddr(i2c, (uint32_t)slave << 1);
	}

	LL_I2C_SetTransferRequest(i2c, write ? LL_I2C_REQUEST_WRITE : LL_I2C_REQUEST_READ);

	/* Always handle end in software */
	LL_I2C_DisableAutoEndMode(i2c);
}

static int tt_stm32_i2c_msg_loop(const struct device *dev, struct i2c_msg *msg, bool force_reload)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;
	unsigned int len = 0U;
	uint8_t *buf = msg->buf;
	bool write = ((msg->flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE);

	/* Will be reset if we see any activity (meaning data transfer) on the bus */
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT));

	len = msg->len;
	while (1) {
		bool buffer_overflow = false;
		bool write_waiting = write && LL_I2C_IsActiveFlag_TXIS(i2c);
		bool read_waiting = !write && LL_I2C_IsActiveFlag_RXNE(i2c);

		if (write_waiting) {
			if (len) {
				timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT));

				LL_I2C_TransmitData8(i2c, *buf);

				buf++;
				len--;
			} else {
				buffer_overflow = true;
			}
		} else if (read_waiting) {
			if (len) {
				timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT));

				*buf = LL_I2C_ReceiveData8(i2c);

				buf++;
				len--;
			} else {
				buffer_overflow = true;
			}
		}

		if (buffer_overflow) {
			LOG_ERR("Buffer Overflow: %d", LL_I2C_GetTransferSize(i2c));
			if (LL_I2C_IsEnabledReloadMode(i2c)) {
				LL_I2C_DisableReloadMode(i2c);
			}
			return -EIO;
		}

		/* If TC is set then we exit, allowing the caller to send a restart (or stop). */
		if (!(write_waiting || read_waiting) && LL_I2C_IsActiveFlag_TC(i2c)) {
			/* No more data to send, just return (caller will cleanup) */
			if (len > 0) {
				LOG_ERR("Message not written before TC: {ts: %d, len: %d}",
					LL_I2C_GetTransferSize(i2c), len);

				if (LL_I2C_IsEnabledReloadMode(i2c)) {
					LL_I2C_DisableReloadMode(i2c);
				}
				return -EIO;
			}
			return 0;
		}

		/* if TCR is set AND we still have data to send; then we reload */
		/* otherwise assume that the caller knows what it's doing and exit */
		if (!(write_waiting || read_waiting) && LL_I2C_IsActiveFlag_TCR(i2c)) {
			int sub = msg->len > STM32_I2C_MAX_TRANSFER_SIZE
					  ? STM32_I2C_MAX_TRANSFER_SIZE
					  : msg->len;
			if (len + sub > msg->len) {
				LOG_ERR("Message not written before reload: {ts: %d, len: %d, sub: "
					"%d}",
					LL_I2C_GetTransferSize(i2c), len, sub);

				if (LL_I2C_IsEnabledReloadMode(i2c)) {
					LL_I2C_DisableReloadMode(i2c);
				}
				return -EIO;
			}
			msg->len -= sub;
			if (msg->len > 0) {
				if (msg->len > STM32_I2C_MAX_TRANSFER_SIZE) {
					LL_I2C_SetTransferSize(i2c, STM32_I2C_MAX_TRANSFER_SIZE);
					LL_I2C_EnableReloadMode(i2c);
				} else {
					LL_I2C_SetTransferSize(i2c, msg->len);

					if (!force_reload) {
						LL_I2C_DisableReloadMode(i2c);
					} else {
						LL_I2C_EnableReloadMode(i2c);
					}
				}

				LL_I2C_DisableAutoEndMode(i2c);
			} else {
				return 0;
			}
		}

		if (check_errors(dev, __func__)) {
			return -EIO;
		}

		if (sys_timepoint_expired(timeout)) {
			LOG_ERR("loop: TIMEOUT");
			return -ETIMEDOUT;
		}

		if (data->current.abort != NULL && *data->current.abort) {
			return -ECANCELED;
		}
	}

	return 0;
}

void tt_stm32_i2c_disable(const struct device *dev)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	if (LL_I2C_IsEnabledReloadMode(i2c)) {
		LL_I2C_DisableReloadMode(i2c);
	}

	if (!data->smbalert_active) {
		i2c_shutdown(dev);
	}
}

/* Send a message; we are assuming that this can only be called in the case where we did not hit the
 */
/* END condition. When a message finishes sneding if stop was not set, then the bus will be waiting
 */
/* for the next start. If stop was sent then you must restart the transfer. After sending a message
 */
/* successfully you must use stop_transfer to release the bus. */
/* */
/* If you specify continue... then we will be foced into reload mode. */
/* How to handle someone changing their mind? If there is a mismatch on the next request then reset
 */
/* I2C? */
int tt_stm32_i2c_send_message(const struct device *dev, uint16_t slave, struct i2c_msg msg,
			      bool start, bool force_reload)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	/* Only send a start if the restart flag is present */
	/* A bit awkward because we also need continue to force the NEXT msg into reload mode. */
	bool restart = (msg.flags & I2C_MSG_RESTART) != 0;
	bool stop = (msg.flags & I2C_MSG_STOP) != 0;

	bool needs_reload = msg.len > STM32_I2C_MAX_TRANSFER_SIZE;

	/* Invalid condition can't be both stop and continue */
	if (stop && force_reload) {
		msg_abort(dev);
		return -EINVAL;
	}

	/* Invalid condition can't restart when reload mode is enabled */
	if (restart && LL_I2C_IsEnabledReloadMode(i2c)) {
		msg_abort(dev);
		return -EINVAL;
	}

	/* Invalid condition we must be in reload mode if we aren't sending a start at the beginning
	 * of
	 */
	/* our message. */
	if (!restart && !LL_I2C_IsEnabledReloadMode(i2c)) {
		msg_abort(dev);
		return -EINVAL;
	}

	/* Assume that we checked this erlier */
	bool write = ((msg.flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE);

	if (restart) {
		if (needs_reload || force_reload) {
			LL_I2C_EnableReloadMode(i2c);
		} else {
			LL_I2C_DisableReloadMode(i2c);
		}

		tt_stm32_i2c_msg_setup(dev, slave, write);

		LL_I2C_SetTransferSize(i2c, msg.len > STM32_I2C_MAX_TRANSFER_SIZE
						    ? STM32_I2C_MAX_TRANSFER_SIZE
						    : msg.len);
		LL_I2C_Enable(i2c);

		LL_I2C_GenerateStartCondition(i2c);
	} else {
		LL_I2C_SetTransferSize(i2c, msg.len > STM32_I2C_MAX_TRANSFER_SIZE
						    ? STM32_I2C_MAX_TRANSFER_SIZE
						    : msg.len);
		if (!(needs_reload || force_reload)) {
			LL_I2C_DisableReloadMode(i2c);
		} else {
			LL_I2C_EnableReloadMode(i2c);
		}
		LL_I2C_DisableAutoEndMode(i2c);
	}

	int ret = tt_stm32_i2c_msg_loop(dev, &msg, force_reload);

	/* Issue stop condition if necessary */
	if (msg.flags & I2C_MSG_STOP) {
		LL_I2C_GenerateStopCondition(i2c);

		/* Infinite loop... make sure we can abort in the case of timeout or the abort flag.
		 */
		k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(CONFIG_TT_I2C_STM32_TIMEOUT));

		while (!LL_I2C_IsActiveFlag_STOP(i2c)) {
			if (data->current.abort != NULL && *data->current.abort) {
				ret = -ECANCELED;
				break;
			}

			if (sys_timepoint_expired(timeout)) {
				LOG_ERR("stop: TIMEOUT");
				ret = -ETIMEDOUT;
				break;
			}
		}

		LL_I2C_ClearFlag_STOP(i2c);
		LL_I2C_DisableReloadMode(i2c);
	}

	if (ret < 0) {
		/* We must now enter the END condition */
		int shutdown_ret = i2c_shutdown(dev);
		/* i2c_shutdown can error; but we already know something went wrong... */
		return ret == 0 ? shutdown_ret : ret;
	}

	return 0;
}
