/*
 * Copyright (c) 2016 BayLibre, SAS
 * Copyright (c) 2017 Linaro Ltd
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <soc.h>
#include <stm32_ll_i2c.h>
#include <stm32_ll_rcc.h>
#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include "tt_stm32_i2c.h"

#ifdef CONFIG_TT_I2C_STM32_BUS_RECOVERY
#include "i2c_bitbang.h"
#endif /* CONFIG_I2C_STM32_BUS_RECOVERY */

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
#include <zephyr/irq.h>
LOG_MODULE_REGISTER(tt_stm32_i2c);

#include "i2c-priv.h"

/* This symbol takes the value 1 if one of the device instances */
/* is configured in dts with a domain clock */
#if STM32_DT_INST_DEV_DOMAIN_CLOCK_SUPPORT
#define STM32_I2C_DOMAIN_CLOCK_SUPPORT 1
#else
#define STM32_I2C_DOMAIN_CLOCK_SUPPORT 0
#endif

int tt_stm32_i2c_get_config(const struct device *dev, uint32_t *config)
{
	struct tt_stm32_i2c_data *data = dev->data;

	if (!data->is_configured) {
		LOG_ERR("I2C controller not configured");
		return -EIO;
	}

	*config = data->dev_config;

	return 0;
}

int tt_stm32_i2c_runtime_configure(const struct device *dev, uint32_t config)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	I2C_TypeDef *i2c = cfg->i2c;
	uint32_t i2c_clock = 0U;
	int ret;

	if (IS_ENABLED(STM32_I2C_DOMAIN_CLOCK_SUPPORT) && (cfg->pclk_len > 1)) {
		if (clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->pclken[1],
					   &i2c_clock) < 0) {
			LOG_ERR("Failed call clock_control_get_rate(pclken[1])");
			return -EIO;
		}
	} else {
		if (clock_control_get_rate(clk, (clock_control_subsys_t)&cfg->pclken[0],
					   &i2c_clock) < 0) {
			LOG_ERR("Failed call clock_control_get_rate(pclken[0])");
			return -EIO;
		}
	}

	data->dev_config = config;

	k_sem_take(&data->bus_mutex, K_FOREVER);

#ifdef CONFIG_PM_DEVICE_RUNTIME
	ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (ret < 0) {
		LOG_ERR("failure Enabling I2C clock");
		return ret;
	}
#endif

	LL_I2C_Disable(i2c);
	tt_stm32_i2c_set_smbus_mode(dev, data->mode);
	ret = tt_stm32_i2c_configure_timing(dev, i2c_clock);

	if (data->smbalert_active) {
		LL_I2C_Enable(i2c);
	}

#ifdef CONFIG_PM_DEVICE_RUNTIME
	ret = clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (ret < 0) {
		LOG_ERR("failure disabling I2C clock");
		return ret;
	}
#endif

	k_sem_give(&data->bus_mutex);

	return ret;
}

#define OPERATION(msg) (((struct i2c_msg *)msg)->flags & I2C_MSG_RW_MASK)

int tt_stm32_i2c_send_messages(const struct device *dev, uint16_t slave, struct i2c_msg *msg,
			       uint8_t num_msgs)
{
	struct i2c_msg *current, *next;
	int ret = 0;

	tt_stm32_i2c_start_transfer(dev);

	current = msg;

	bool start = true;

	while (num_msgs > 0) {
		bool cont = false;

		if (num_msgs > 1) {
			next = current + 1;
			cont = ((next->flags & I2C_MSG_RESTART) == 0) &&
			       ((current->flags & I2C_MSG_STOP) == 0) &&
			       (OPERATION(current) == OPERATION(next));
		}

		ret = tt_stm32_i2c_send_message(dev, slave, *current, start, cont);
		start = false;

		if (ret < 0) {
			break;
		}
		current++;
		num_msgs--;
	}

	tt_stm32_i2c_stop_transfer(dev);

	return ret;
}

static int tt_stm32_i2c_transfer(const struct device *dev, struct i2c_msg *msg, uint8_t num_msgs,
				 uint16_t slave)
{
	struct i2c_msg *current, *next;
	int ret = 0;

	/* Check for validity of all messages, to prevent having to abort
	 * in the middle of a transfer
	 */
	current = msg;

	/*
	 * Set I2C_MSG_RESTART flag on first message in order to send start
	 * condition
	 */
	current->flags |= I2C_MSG_RESTART;

	for (uint8_t i = 1; i <= num_msgs; i++) {

		if (i < num_msgs) {
			next = current + 1;

			/*
			 * Restart condition between messages
			 * of different directions is required
			 */
			if (OPERATION(current) != OPERATION(next)) {
				if (!(next->flags & I2C_MSG_RESTART)) {
					ret = -EINVAL;
					break;
				}
			}

			/* Stop condition is only allowed on last message */
			if (current->flags & I2C_MSG_STOP) {
				ret = -EINVAL;
				break;
			}
		} else {
			/* Stop condition is required for the last message */
			if (!(current->flags & I2C_MSG_STOP)) {
				ret = -EINVAL;
				break;
			}
		}

		current++;
	}

	if (ret) {
		return ret;
	}

	return tt_stm32_i2c_send_messages(dev, slave, msg, num_msgs);
}

void tt_stm32_i2c_start_transfer(const struct device *dev)
{
	struct tt_stm32_i2c_data *data = dev->data;

	/* Send out messages */
	k_sem_take(&data->bus_mutex, K_FOREVER);

	/* Prevent driver from being suspended by PM until I2C transaction is complete */
#ifdef CONFIG_PM_DEVICE_RUNTIME
	(void)pm_device_runtime_get(dev);
#endif

	/* Prevent the clocks to be stopped during the i2c transaction */
	pm_policy_state_lock_get(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);

	tt_stm32_i2c_enable(dev);
}

void tt_stm32_i2c_stop_transfer(const struct device *dev)
{
	struct tt_stm32_i2c_data *data = dev->data;

	tt_stm32_i2c_disable(dev);

	pm_policy_state_lock_put(PM_STATE_SUSPEND_TO_IDLE, PM_ALL_SUBSTATES);

#ifdef CONFIG_PM_DEVICE_RUNTIME
	(void)pm_device_runtime_put(dev);
#endif

	k_sem_give(&data->bus_mutex);
}

#if CONFIG_TT_I2C_STM32_BUS_RECOVERY
static void tt_stm32_i2c_bitbang_set_scl(void *io_context, int state)
{
	const struct tt_stm32_i2c_config *config = io_context;

	gpio_pin_set_dt(&config->scl, state);
}

static void tt_stm32_i2c_bitbang_set_sda(void *io_context, int state)
{
	const struct tt_stm32_i2c_config *config = io_context;

	gpio_pin_set_dt(&config->sda, state);
}

static int tt_stm32_i2c_bitbang_get_sda(void *io_context)
{
	const struct tt_stm32_i2c_config *config = io_context;

	return gpio_pin_get_dt(&config->sda) == 0 ? 0 : 1;
}

static int tt_stm32_i2c_recover_bus(const struct device *dev)
{
	const struct tt_stm32_i2c_config *config = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	struct i2c_bitbang bitbang_ctx;
	struct i2c_bitbang_io bitbang_io = {
		.set_scl = tt_stm32_i2c_bitbang_set_scl,
		.set_sda = tt_stm32_i2c_bitbang_set_sda,
		.get_sda = tt_stm32_i2c_bitbang_get_sda,
	};
	uint32_t bitrate_cfg;
	int error = 0;

	LOG_ERR("attempting to recover bus");

	if (!gpio_is_ready_dt(&config->scl)) {
		LOG_ERR("SCL GPIO device not ready");
		return -EIO;
	}

	if (!gpio_is_ready_dt(&config->sda)) {
		LOG_ERR("SDA GPIO device not ready");
		return -EIO;
	}

	k_sem_take(&data->bus_mutex, K_FOREVER);

	error = gpio_pin_configure_dt(&config->scl, GPIO_OUTPUT_HIGH);
	if (error != 0) {
		LOG_ERR("failed to configure SCL GPIO (err %d)", error);
		goto restore;
	}

	error = gpio_pin_configure_dt(&config->sda, GPIO_OUTPUT_HIGH);
	if (error != 0) {
		LOG_ERR("failed to configure SDA GPIO (err %d)", error);
		goto restore;
	}

	i2c_bitbang_init(&bitbang_ctx, &bitbang_io, (void *)config);

	bitrate_cfg = i2c_map_dt_bitrate(config->bitrate) | I2C_MODE_CONTROLLER;
	error = i2c_bitbang_configure(&bitbang_ctx, bitrate_cfg);
	if (error != 0) {
		LOG_ERR("failed to configure I2C bitbang (err %d)", error);
		goto restore;
	}

	error = i2c_bitbang_recover_bus(&bitbang_ctx);
	if (error != 0) {
		LOG_ERR("failed to recover bus (err %d)", error);
	}

restore:
	(void)pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);

	k_sem_give(&data->bus_mutex);

	return error;
}
#endif /* CONFIG_I2C_STM32_BUS_RECOVERY */

static const struct i2c_driver_api api_funcs = {
	.configure = tt_stm32_i2c_runtime_configure,
	.transfer = tt_stm32_i2c_transfer,
	.get_config = tt_stm32_i2c_get_config,
#if CONFIG_TT_I2C_STM32_BUS_RECOVERY
	.recover_bus = tt_stm32_i2c_recover_bus,
#endif /* CONFIG_I2C_STM32_BUS_RECOVERY */
};

#ifdef CONFIG_PM_DEVICE

static int tt_stm32_i2c_suspend(const struct device *dev)
{
	int ret;
	const struct tt_stm32_i2c_config *cfg = dev->config;
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);

	/* Disable device clock. */
	ret = clock_control_off(clk, (clock_control_subsys_t)&cfg->pclken[0]);
	if (ret < 0) {
		LOG_ERR("failure disabling I2C clock");
		return ret;
	}

	/* Move pins to sleep state */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_SLEEP);
	if (ret == -ENOENT) {
		/* Warn but don't block suspend */
		LOG_WRN("I2C pinctrl sleep state not available ");
	} else if (ret < 0) {
		return ret;
	}

	return 0;
}

#endif

static int tt_stm32_i2c_activate(const struct device *dev)
{
	int ret;
	const struct tt_stm32_i2c_config *cfg = dev->config;
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);

	/* Move pins to active/default state */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("I2C pinctrl setup failed (%d)", ret);
		return ret;
	}

	/* Enable device clock. */
	if (clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[0]) != 0) {
		LOG_ERR("i2c: failure enabling clock");
		return -EIO;
	}

	return 0;
}

static int tt_stm32_i2c_init(const struct device *dev)
{
	const struct device *const clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	const struct tt_stm32_i2c_config *cfg = dev->config;
	uint32_t bitrate_cfg;
	int ret;
	struct tt_stm32_i2c_data *data = dev->data;
#ifdef CONFIG_TT_I2C_STM32_INTERRUPT
	k_sem_init(&data->device_sync_sem, 0, K_SEM_MAX_LIMIT);
	cfg->irq_config_func(dev);
#endif

	data->is_configured = false;
	data->mode = I2CSTM32MODE_I2C;

	/*
	 * initialize mutex used when multiple transfers
	 * are taking place to guarantee that each one is
	 * atomic and has exclusive access to the I2C bus.
	 */
	k_sem_init(&data->bus_mutex, 1, 1);

	if (!device_is_ready(clk)) {
		LOG_ERR("clock control device not ready");
		return -ENODEV;
	}

	tt_stm32_i2c_activate(dev);

	if (IS_ENABLED(STM32_I2C_DOMAIN_CLOCK_SUPPORT) && (cfg->pclk_len > 1)) {
		/* Enable I2C clock source */
		ret = clock_control_configure(clk, (clock_control_subsys_t)&cfg->pclken[1], NULL);
		if (ret < 0) {
			return -EIO;
		}
	}

#if defined(CONFIG_SOC_SERIES_STM32F1X)
	/*
	 * Force i2c reset for STM32F1 series.
	 * So that they can enter master mode properly.
	 * Issue described in ES096 2.14.7
	 */
	I2C_TypeDef *i2c = cfg->i2c;

	LL_I2C_EnableReset(i2c);
	LL_I2C_DisableReset(i2c);
#endif

	bitrate_cfg = i2c_map_dt_bitrate(cfg->bitrate);

	ret = tt_stm32_i2c_runtime_configure(dev, I2C_MODE_CONTROLLER | bitrate_cfg);
	if (ret < 0) {
		LOG_ERR("i2c: failure initializing");
		return ret;
	}

#ifdef CONFIG_PM_DEVICE_RUNTIME
	(void)pm_device_runtime_enable(dev);
#endif

	data->is_configured = true;

	return 0;
}

void tt_stm32_i2c_set_abort_ptr(const struct device *dev, unsigned int *abort)
{
	struct tt_stm32_i2c_data *data = dev->data;

	data->abort = abort;
}

#ifdef CONFIG_PM_DEVICE

static int tt_stm32_i2c_pm_action(const struct device *dev, enum pm_device_action action)
{
	int err;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		err = tt_stm32_i2c_activate(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		err = tt_stm32_i2c_suspend(dev);
		break;
	default:
		return -ENOTSUP;
	}

	return err;
}

#endif

#ifdef CONFIG_SMBUS_STM32_SMBALERT
void tt_stm32_i2c_smbalert_set_callback(const struct device *dev, i2c_stm32_smbalert_cb_func_t func,
					const struct device *cb_dev)
{
	struct tt_stm32_i2c_data *data = dev->data;

	data->smbalert_cb_func = func;
	data->smbalert_cb_dev = cb_dev;
}
#endif /* CONFIG_SMBUS_STM32_SMBALERT */

void tt_stm32_i2c_set_smbus_mode(const struct device *dev, enum i2c_stm32_mode mode)
{
	const struct tt_stm32_i2c_config *cfg = dev->config;
	struct tt_stm32_i2c_data *data = dev->data;
	I2C_TypeDef *i2c = cfg->i2c;

	data->mode = mode;

	switch (mode) {
	case I2CSTM32MODE_I2C:
		LL_I2C_SetMode(i2c, LL_I2C_MODE_I2C);
		return;
#ifdef CONFIG_SMBUS_STM32
	case I2CSTM32MODE_SMBUSHOST:
		LL_I2C_SetMode(i2c, LL_I2C_MODE_SMBUS_HOST);
		return;
	case I2CSTM32MODE_SMBUSDEVICE:
		LL_I2C_SetMode(i2c, LL_I2C_MODE_SMBUS_DEVICE);
		return;
	case I2CSTM32MODE_SMBUSDEVICEARP:
		LL_I2C_SetMode(i2c, LL_I2C_MODE_SMBUS_DEVICE_ARP);
		return;
#endif
	default:
		LOG_ERR("%s: invalid mode %i", dev->name, mode);
		return;
	}
}

#ifdef CONFIG_SMBUS_STM32
void tt_stm32_i2c_smbalert_enable(const struct device *dev)
{
	struct tt_stm32_i2c_data *data = dev->data;
	const struct tt_stm32_i2c_config *cfg = dev->config;

	data->smbalert_active = true;
	LL_I2C_EnableSMBusAlert(cfg->i2c);
	LL_I2C_EnableIT_ERR(cfg->i2c);
	LL_I2C_Enable(cfg->i2c);
}

void tt_stm32_i2c_smbalert_disable(const struct device *dev)
{
	struct tt_stm32_i2c_data *data = dev->data;
	const struct tt_stm32_i2c_config *cfg = dev->config;

	data->smbalert_active = false;
	LL_I2C_DisableSMBusAlert(cfg->i2c);
	LL_I2C_DisableIT_ERR(cfg->i2c);
	LL_I2C_Disable(cfg->i2c);
}
#endif /* CONFIG_SMBUS_STM32 */

/* Macros for I2C instance declaration */

#define DT_DRV_COMPAT st_tt_stm32_i2c

#ifdef CONFIG_TT_I2C_STM32_INTERRUPT

#ifdef CONFIG_TT_I2C_STM32_COMBINED_INTERRUPT
#define STM32_I2C_IRQ_CONNECT_AND_ENABLE(index)                                                    \
	do {                                                                                       \
		IRQ_CONNECT(DT_INST_IRQN(index), DT_INST_IRQ(index, priority),                     \
			    tt_stm32_i2c_combined_isr, DEVICE_DT_INST_GET(index), 0);              \
		irq_enable(DT_INST_IRQN(index));                                                   \
	} while (false)
#else
#define STM32_I2C_IRQ_CONNECT_AND_ENABLE(index)                                                    \
	do {                                                                                       \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(index, event, irq),                                \
			    DT_INST_IRQ_BY_NAME(index, event, priority), tt_stm32_i2c_event_isr,   \
			    DEVICE_DT_INST_GET(index), 0);                                         \
		irq_enable(DT_INST_IRQ_BY_NAME(index, event, irq));                                \
                                                                                                   \
		IRQ_CONNECT(DT_INST_IRQ_BY_NAME(index, error, irq),                                \
			    DT_INST_IRQ_BY_NAME(index, error, priority), tt_stm32_i2c_error_isr,   \
			    DEVICE_DT_INST_GET(index), 0);                                         \
		irq_enable(DT_INST_IRQ_BY_NAME(index, error, irq));                                \
	} while (false)
#endif /* CONFIG_I2C_STM32_COMBINED_INTERRUPT */

#define STM32_I2C_IRQ_HANDLER_DECL(index)                                                          \
	static void i2c_stm32_irq_config_func_##index(const struct device *dev)
#define STM32_I2C_IRQ_HANDLER_FUNCTION(index) .irq_config_func = i2c_stm32_irq_config_func_##index,
#define STM32_I2C_IRQ_HANDLER(index)                                                               \
	static void i2c_stm32_irq_config_func_##index(const struct device *dev)                    \
	{                                                                                          \
		STM32_I2C_IRQ_CONNECT_AND_ENABLE(index);                                           \
	}
#else

#define STM32_I2C_IRQ_HANDLER_DECL(index)
#define STM32_I2C_IRQ_HANDLER_FUNCTION(index)
#define STM32_I2C_IRQ_HANDLER(index)

#endif /* CONFIG_I2C_STM32_INTERRUPT */

#define STM32_I2C_INIT(index)                                                                      \
	STM32_I2C_IRQ_HANDLER_DECL(index);                                                         \
                                                                                                   \
	static const uint32_t i2c_timings_##index[] = DT_INST_PROP_OR(index, timings, {});         \
                                                                                                   \
	PINCTRL_DT_INST_DEFINE(index);                                                             \
                                                                                                   \
	static const struct stm32_pclken pclken_##index[] = STM32_DT_INST_CLOCKS(index);           \
                                                                                                   \
	static const struct tt_stm32_i2c_config i2c_stm32_cfg_##index = {                          \
		.i2c = (I2C_TypeDef *)DT_INST_REG_ADDR(index),                                     \
		.pclken = pclken_##index,                                                          \
		.pclk_len = DT_INST_NUM_CLOCKS(index),                                             \
		STM32_I2C_IRQ_HANDLER_FUNCTION(index).bitrate =                                    \
			DT_INST_PROP(index, clock_frequency),                                      \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(index),                                     \
		IF_ENABLED(CONFIG_TT_I2C_STM32_SELECT_GPIOS,                                       \
	       (.scl = GPIO_DT_SPEC_INST_GET_OR(index, scl_gpios, {0}),                            \
		.sda = GPIO_DT_SPEC_INST_GET_OR(index, sda_gpios, {0}),)) .timings =               \
						    (const struct tt_i2c_config_timing *)          \
							    i2c_timings_##index,                   \
				    .n_timings = ARRAY_SIZE(i2c_timings_##index),                  \
	};                                                                                         \
                                                                                                   \
	static struct tt_stm32_i2c_data i2c_stm32_dev_data_##index;                                \
                                                                                                   \
	PM_DEVICE_DT_INST_DEFINE(index, tt_stm32_i2c_pm_action);                                   \
                                                                                                   \
	I2C_DEVICE_DT_INST_DEFINE(index, tt_stm32_i2c_init, PM_DEVICE_DT_INST_GET(index),          \
				  &i2c_stm32_dev_data_##index, &i2c_stm32_cfg_##index,             \
				  POST_KERNEL, CONFIG_I2C_INIT_PRIORITY, &api_funcs);              \
                                                                                                   \
	STM32_I2C_IRQ_HANDLER(index)

DT_INST_FOREACH_STATUS_OKAY(STM32_I2C_INIT)
