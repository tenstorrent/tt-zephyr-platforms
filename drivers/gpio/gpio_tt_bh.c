/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_gpio

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <errno.h>

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_tt_bh);

struct gpio_tt_bh_config {
	const struct gpio_driver_config common;

	uint32_t trien_addr;
	uint32_t rxen_addr;
	uint32_t data_addr;
};

struct gpio_tt_bh_data {
	struct gpio_driver_data common;

	gpio_flags_t *flags;

	uint32_t output_enabled;

	struct k_spinlock lock;
};

static int gpio_tt_bh_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	int ret = 0;
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	if ((BIT(pin) & config->common.port_pin_mask) == 0) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		uint32_t trien = sys_read32(config->trien_addr);
		uint32_t rxen = sys_read32(config->rxen_addr);

		if (flags & GPIO_OUTPUT) {
			trien &= ~(1 << pin);
			rxen &= ~(1 << pin);
			data->output_enabled |= BIT(pin);
		} else if (flags & GPIO_INPUT) {
			trien |= (1 << pin);
			rxen |= (1 << pin);
			data->output_enabled &= ~BIT(pin);
		} else {
			ret = -ENOTSUP;
			K_SPINLOCK_BREAK;
		}

		sys_write32(trien, config->trien_addr);
		sys_write32(rxen, config->rxen_addr);
	}

	return ret;
}

#ifdef CONFIG_GPIO_GET_CONFIG
static int gpio_tt_bh_pin_get_config(const struct device *port, gpio_pin_t pin, gpio_flags_t *flags)
{
	if ((BIT(pin) & config->common.port_pin_mask) == 0) {
		return -EINVAL;
	}

	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		uint32_t trien_val = sys_read32(config->trien_addr);
		bool is_input = (trien_val >> pin) & 0x1;

		*flags = is_input ? GPIO_INPUT : GPIO_OUTPUT;
	}

	return 0;
}
#endif /* CONFIG_GPIO_GET_CONFIG */

static int gpio_tt_bh_port_get_raw(const struct device *port, gpio_port_value_t *value)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		*value = sys_read32(config->data_addr);
	}
	
	return 0;
}

static int gpio_tt_bh_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {	
		mask &= data->output_enabled;
		
		uint32_t data_val = sys_read32(config->data_addr);

		data_val = (data_val & ~mask) | (value & mask);
		sys_write32(data_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		pins &= data->output_enabled;

		uint32_t data_val = sys_read32(config->data_addr);

		data_val |= pins;
		sys_write32(data_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		pins &= data->output_enabled;

		uint32_t data_val = sys_read32(config->data_addr);

		data_val &= ~pins;
		sys_write32(data_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		pins &= data->output_enabled;

		uint32_t data_val = sys_read32(config->data_addr);

		data_val ^= pins;
		sys_write32(data_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
					      enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	/* TODO: Add interrupt support */
	return -ENOSYS;
}

static int gpio_tt_bh_manage_callback(const struct device *port, struct gpio_callback *cb, bool set)
{
	/* TODO: Add interrupt support */
	return -ENOSYS;
}

static uint32_t gpio_tt_bh_get_pending_int(const struct device *dev)
{
	/* TODO: Add interrupt support */
	return -ENOSYS;
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_tt_bh_port_get_direction(const struct device *port, gpio_port_pins_t map,
					 gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		uint32_t trien_val = sys_read32(config->trien_addr);

		*inputs = trien_val & map;
		*outputs = ~trien_val & map;
	}

	return 0;
}
#endif /* CONFIG_GPIO_GET_DIRECTION */

static DEVICE_API(gpio, gpio_tt_bh_driver) = {
	.pin_configure = gpio_tt_bh_pin_configure,
#ifdef CONFIG_GPIO_GET_CONFIG
	.pin_get_config = gpio_tt_bh_pin_get_config,
#endif /* CONFIG_GPIO_GET_CONFIG */
	.port_get_raw = gpio_tt_bh_port_get_raw,
	.port_set_masked_raw = gpio_tt_bh_port_set_masked_raw,
	.port_set_bits_raw = gpio_tt_bh_port_set_bits_raw,
	.port_clear_bits_raw = gpio_tt_bh_port_clear_bits_raw,
	.port_toggle_bits = gpio_tt_bh_port_toggle_bits,
	.pin_interrupt_configure = gpio_tt_bh_pin_interrupt_configure,
	.manage_callback = gpio_tt_bh_manage_callback,
	.get_pending_int = gpio_tt_bh_get_pending_int,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_tt_bh_port_get_direction,
#endif /* CONFIG_GPIO_GET_DIRECTION */
};

static int gpio_tt_bh_init(const struct device *dev)
{
	return 0;
}

#define DEFINE_GPIO_TT_BH(_num)                                                                    \
	static gpio_flags_t gpio_tt_bh_flags_##_num[DT_INST_PROP(_num, ngpios)];                   \
                                                                                                   \
	static const struct gpio_tt_bh_config gpio_tt_bh_config_##_num = {                         \
		.common =                                                                          \
			{                                                                          \
				.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(_num),            \
			},                                                                         \
		.trien_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), trien),                       \
		.rxen_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), rxen),                         \
		.data_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), data),                         \
	};                                                                                         \
                                                                                                   \
	static struct gpio_tt_bh_data gpio_tt_bh_data_##_num = {                                   \
		.flags = gpio_tt_bh_flags_##_num,                                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_num, gpio_tt_bh_init, PM_DEVICE_DT_INST_GET(_num),                  \
			      &gpio_tt_bh_data_##_num, &gpio_tt_bh_config_##_num, POST_KERNEL,     \
			      CONFIG_GPIO_INIT_PRIORITY, &gpio_tt_bh_driver);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_GPIO_TT_BH)
