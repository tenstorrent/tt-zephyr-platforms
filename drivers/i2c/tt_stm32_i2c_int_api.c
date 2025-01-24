/*
 * Copyright (c) 2016 BayLibre, SAS
 * Copyright (c) 2017 Linaro Ltd
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * I2C Driver for: STM32G0
 *
 */

#error "This is untested code.... please test before enabling"

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
#include "tt_i2c_stm32.h"
#include "stm32g0xx_ll_i2c.h"
#include "zephyr/sys_clock.h"

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tt_i2c_stm32_api);

#include "i2c-priv.h"

#define STM32_I2C_TRANSFER_TIMEOUT_MSEC 500

int tt_stm32_i2c_configure_timing(const struct device *dev, uint32_t clock)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
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

static void tt_stm32_i2c_disable_transfer_interrupts(const struct device *dev)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_DisableIT_TX(i2c);
	LL_I2C_DisableIT_RX(i2c);
	LL_I2C_DisableIT_STOP(i2c);
	LL_I2C_DisableIT_NACK(i2c);
	LL_I2C_DisableIT_TC(i2c);

	if (!data->smbalert_active) {
		LL_I2C_DisableIT_ERR(i2c);
	}
}

static void tt_stm32_i2c_enable_transfer_interrupts(const struct device *dev, bool write)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_EnableIT_STOP(i2c);
	LL_I2C_EnableIT_NACK(i2c);
	LL_I2C_EnableIT_TC(i2c);
	LL_I2C_EnableIT_ERR(i2c);
	if (write) {
		LL_I2C_EnableIT_TX(i2c);
		LL_I2C_DisableIT_RX(i2c);
	} else {
		LL_I2C_DisableIT_TX(i2c);
		LL_I2C_DisableIT_RX(i2c);
	}
}

/* StateMachine */
/* During sending */
/*  1.TXIS flag is set after each byte transmission, afer the 9th SCL pulse when the ACK is */
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

static void tt_stm32_i2c_event(const struct device *dev)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	/* Received external abort signal */
	if (data->current.abort != NULL && *data->current.abort) {
		LL_I2C_GenerateStopCondition(i2c);
		goto end_i2c;
	}

	/* Make sure we aren't overwriting out buffer... */
	/* If we are waiting for data but don't have anymore room in our buffer we should abort */
	bool tx_empty = LL_I2C_IsActiveFlag_TXIS(i2c);
	bool rx_full = LL_I2C_IsActiveFlag_RXNE(i2c);
	bool data_waiting = tx_empty || rx_full;

	if (data_waiting) {
		/* We are about to overflow our buffer (abort!!) */
		/* During abort should we be sending some stop or NACK condition? */
		if (data->current.len == 0) {
			data->current.buffer_overflow = 1U;
			goto end_i2c;
		}

		/* Send next byte */
		if (tx_empty) {
			LL_I2C_TransmitData8(i2c, *data->current.buf);
		}
		/* Receive next byte */
		else if (rx_full) {
			*data->current.buf = LL_I2C_ReceiveData8(i2c);
		}

		data->current.buf++;
		data->current.len--;
	}

	/* NACK received */
	if (LL_I2C_IsActiveFlag_NACK(i2c)) {
		LL_I2C_ClearFlag_NACK(i2c);
		data->current.is_nack = 1U;

		/* Make sure we don't screw up the state machine? */
		LL_I2C_DisableReloadMode(i2c);

		/*
		 * AutoEndMode is always disabled in master mode,
		 * so send a stop condition manually
		 */
		LL_I2C_GenerateStopCondition(i2c);

		/* We aren't sending anymore data, so return back to the main loop for handling. */
		goto end_i2c;
	}

	/* STOP received */
	if (LL_I2C_IsActiveFlag_STOP(i2c)) {
		LL_I2C_ClearFlag_STOP(i2c);

		/* Make sure we don't screw up the state machine? */
		LL_I2C_DisableReloadMode(i2c);

		/* We probably didn't expect stop... */
		/* return back to the main loop for handling. */
		goto end_i2c;
	}

	/* Transfer Complete or Transfer Complete Reload */
	if (LL_I2C_IsActiveFlag_TC(i2c) || LL_I2C_IsActiveFlag_TCR(i2c)) {
		/* Transfer complete, it could either be TC or TCR depending on if we have */
		/* reload enabled. We'll handle that reload/restart/stop case in the main loop. */
		/* So just exit here. */

		/* We won't clear these, because the main loop is expected to handle this case. */
		goto end_i2c;
	}

	return;
end_i2c:
	/* We called into this by taking a semaphore; release it so the main thread can */
	/* reset i2c and disable the interrupts */
	tt_stm32_i2c_disable_transfer_interrupts(dev);
	k_sem_give(&data->device_sync_sem);
}

static int tt_stm32_i2c_error(const struct device *dev)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	if (LL_I2C_IsActiveFlag_ARLO(i2c)) {
		LL_I2C_ClearFlag_ARLO(i2c);
		data->current.is_arlo = 1U;
		goto end;
	}

	if (LL_I2C_IsActiveFlag_BERR(i2c)) {
		LL_I2C_ClearFlag_BERR(i2c);
		data->current.is_err = 1U;
		goto end;
	}

#if defined(CONFIG_SMBUS_STM32_SMBALERT)
	if (LL_I2C_IsActiveSMBusFlag_ALERT(i2c)) {
		LL_I2C_ClearSMBusFlag_ALERT(i2c);
		if (data->smbalert_cb_func != NULL) {
			data->smbalert_cb_func(data->smbalert_cb_dev);
		}
		goto end;
	}
#endif
	return 0;
end:
	return -EIO;
}

#ifdef CONFIG_TT_I2C_STM32_COMBINED_INTERRUPT
void tt_stm32_i2c_combined_isr(void *arg)
{
	const struct device *dev = (const struct device *)arg;

	if (tt_stm32_i2c_error(dev)) {
		return;
	}
	tt_stm32_i2c_event(dev);
}
#else

void tt_stm32_i2c_event_isr(void *arg)
{
	const struct device *dev = (const struct device *)arg;

	tt_stm32_i2c_event(dev);
}

void tt_stm32_i2c_error_isr(void *arg)
{
	const struct device *dev = (const struct device *)arg;

	tt_stm32_i2c_error(dev);
}
#endif

static void tt_stm32_reset_i2c(const struct device *dev)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_Disable(i2c);
	while (LL_I2C_IsEnabled(i2c)) {
	}
	LL_I2C_Enable(i2c);
}

void tt_stm32_i2c_stop_transfer(const struct device *dev)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	tt_stm32_i2c_disable_transfer_interrupts(dev);

	/* We want the semaphore to stall the next time k_sem_take is run */
	k_sem_reset(&data->device_sync_sem);

	if (LL_I2C_IsEnabledReloadMode(i2c)) {
		LL_I2C_DisableReloadMode(i2c);
	}

	if (!data->smbalert_active) {
		LL_I2C_Disable(i2c);
	}
}

static int tt_stm32_i2c_msg_impl(const struct device *dev, struct i2c_msg *msg, bool write)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;
	bool is_timeout = false;

	data->current.len = msg->len;
	data->current.buf = msg->buf;
	data->current.is_write = write ? 1U : 0U;
	data->current.buffer_overflow = 0U;
	data->current.is_slave_stop = 0U;
	data->current.is_arlo = 0U;
	data->current.is_err = 0U;
	data->current.is_nack = 0U;
	data->current.abort = data->abort;
	data->current.msg = msg;

	tt_stm32_i2c_enable_transfer_interrupts(dev, write);

	if (k_sem_take(&data->device_sync_sem, K_MSEC(STM32_I2C_TRANSFER_TIMEOUT_MSEC)) != 0) {
		is_timeout = true;
	}

	if (data->current.is_slave_stop || data->current.buffer_overflow || data->current.is_nack ||
	    data->current.is_err || data->current.is_arlo || is_timeout) {
		goto error;
	}

	return 0;
error:
	if (data->current.is_slave_stop) {
		LOG_ERR("%s: Slave Stop %d", __func__, data->current.is_slave_stop);
		data->current.is_slave_stop = 0U;
	}

	if (data->current.buffer_overflow) {
		LOG_ERR("%s: Buffer Overflow %d", __func__, data->current.buffer_overflow);
		data->current.is_slave_stop = 0U;
	}

	if (data->current.is_arlo) {
		LOG_DBG("%s: ARLO %d", __func__, data->current.is_arlo);
		data->current.is_slave_stop = 0U;
	}

	if (data->current.is_arlo) {
		LOG_DBG("%s: ARLO %d", __func__, data->current.is_arlo);
		data->current.is_arlo = 0U;
	}

	if (data->current.is_nack) {
		LOG_DBG("%s: NACK", __func__);
		data->current.is_nack = 0U;
	}

	if (data->current.is_err) {
		LOG_DBG("%s: ERR %d", __func__, data->current.is_err);
		data->current.is_err = 0U;
	}

	if (is_timeout) {
		LOG_DBG("%s: TIMEOUT", __func__);
	}

	LL_I2C_GenerateStopCondition(i2c);

	return -EIO;
}

static void tt_stm32_i2c_msg_setup(const struct device *dev, uint16_t slave, bool write)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
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

/* Send a message; we are assuming that this can only be called in the case where we did not hit the
 */
/* END condition. When a message finishes sneding if stop was not set, then the bus will be waiting
 */
/* for the next start. If stop was sent then you must restart the transfer. After sending a message
 */
/* successfully you must use stop_transfer to release the bus. */
int tt_stm32_i2c_send_message(const struct device *dev, uint16_t slave, struct i2c_msg msg)
{
	const struct tt_i2c_stm32_config *cfg = dev->config;
	struct tt_i2c_stm32_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	const uint32_t i2c_stm32_maxchunk = 255U;

	/* In order to support the case where we might want to handle reads that requre us to make a
	 * decision based on a read for example SMBus BlockRead require that restart is set in order
	 * to send an address + start
	 */
	bool restart = (msg.flags & I2C_MSG_RESTART) != 0;

	/* To ensure that we are not expecting to reprogram address or reload settings do some
	 * validation
	 * Based on the reference, because we are setting NBYTE we will always send a NACK even if
	 * we don't reload the buffer.
	 */
	bool needs_reload = msg.len > i2c_stm32_maxchunk;

	if (!restart && needs_reload && LL_I2C_IsEnabledReloadMode(i2c)) {
		tt_stm32_i2c_stop_transfer(dev);
		return -EINVAL;
	}

	/* I2C is off and we would have to send a start bit */
	if (!restart && !LL_I2C_IsEnabled(i2c)) {
		tt_stm32_i2c_stop_transfer(dev);
		return -EINVAL;
	}

	/* Assume that we checked this erlier */
	bool write = ((msg.flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE);

	if (restart) {
		if (needs_reload) {
			LL_I2C_EnableReloadMode(i2c);
		} else {
			LL_I2C_DisableReloadMode(i2c);
		}

		bool write = ((msg.flags & I2C_MSG_RW_MASK) == I2C_MSG_WRITE);

		tt_stm32_i2c_msg_setup(dev, slave, write);

		LL_I2C_SetTransferSize(i2c, msg.len > i2c_stm32_maxchunk ? 0xFF : msg.len);

		if (!LL_I2C_IsEnabled(i2c)) {
			LL_I2C_Enable(i2c);
		}

		LL_I2C_GenerateStartCondition(i2c);
	}

	do {
		int ret = tt_stm32_i2c_msg_impl(dev, &msg, write);

		/* We must now enter the END condition */
		if (ret < 0 || data->current.is_slave_stop) {
			tt_stm32_i2c_stop_transfer(dev);
			return ret;
		}

		/* Reset msg->len if we need to reload */
		msg->len -= msg->len > i2c_stm32_maxchunk ? i2c_stm32_maxchunk : msg->len;
		if (msg->len > 0) {
			LL_I2C_SetTransferSize(i2c,
					       msg->len > i2c_stm32_maxchunk ? 0xFF : msg->len);
		}
	} while (msg.len > 0);

	if (msg.flags & I2C_MSG_STOP) {
		LL_I2C_GenerateStopCondition(i2c);
	}

	return 0;
}
