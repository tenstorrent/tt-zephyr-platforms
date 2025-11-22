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

	uintptr_t trien_addr;
	uintptr_t rxen_addr;
	uintptr_t data_addr;

	uint8_t ngpios;
};

struct gpio_tt_bh_data {
	struct gpio_driver_data common;
	struct k_spinlock lock;
};

static int gpio_tt_bh_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	/* Only support input and output flags */
	if ((flags & ~(GPIO_INPUT | GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH |
		       GPIO_DISCONNECTED)) != 0) {
		return -ENOTSUP;
	}

	/* Does not support simultaneous input/output */
	if ((flags & GPIO_INPUT) && (flags & GPIO_OUTPUT)) {
		return -ENOTSUP;
	}

	if (pin >= config->ngpios) {
		return -EINVAL;
	}

	K_SPINLOCK(&data->lock) {
		/* Configure input */

		uint32_t rxen_val = sys_read32(config->rxen_addr);

		if (flags & GPIO_INPUT) {
			rxen_val |= BIT(pin);
		} else {
			rxen_val &= ~BIT(pin);
		}

		sys_write32(rxen_val, config->rxen_addr);

		/* Configure output */

		uint32_t trien_val = sys_read32(config->trien_addr);
		uint32_t data_val = sys_read32(config->data_addr);

		if (flags & GPIO_OUTPUT) {
			trien_val &= ~BIT(pin);

			/* Initialize pin to low or high state for output configuration */

			if (flags & GPIO_OUTPUT_INIT_HIGH) {
				data_val |= BIT(pin);
			} else if (flags & GPIO_OUTPUT_INIT_LOW) {
				data_val &= ~BIT(pin);
			}

			sys_write32(data_val, config->data_addr);
		} else {
			trien_val |= BIT(pin);
		}

		sys_write32(trien_val, config->trien_addr);
	}

	return 0;
}

#ifdef CONFIG_GPIO_GET_CONFIG
static int gpio_tt_bh_pin_get_config(const struct device *port, gpio_pin_t pin, gpio_flags_t *flags)
{
	return -ENOTSUP;
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
		uintptr_t current_val = sys_read32(config->data_addr);
		uintptr_t new_val = (current_val & ~mask) | (value & mask);

		sys_write32(new_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_tt_bh_config *config = (const struct gpio_tt_bh_config *)port->config;
	struct gpio_tt_bh_data *data = (struct gpio_tt_bh_data *)port->data;

	K_SPINLOCK(&data->lock) {
		uintptr_t data_val = sys_read32(config->data_addr);

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
		uintptr_t data_val = sys_read32(config->data_addr);

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
		uintptr_t data_val = sys_read32(config->data_addr);

		data_val ^= pins;
		sys_write32(data_val, config->data_addr);
	}

	return 0;
}

static int gpio_tt_bh_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
					      enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	return -ENOTSUP;
}

static int gpio_tt_bh_manage_callback(const struct device *port, struct gpio_callback *cb, bool set)
{
	return -ENOTSUP;
}

static uint32_t gpio_tt_bh_get_pending_int(const struct device *dev)
{
	return 0;
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_tt_bh_port_get_direction(const struct device *port, gpio_port_pins_t map,
					 gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	return -ENOTSUP;
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
	static const struct gpio_tt_bh_config gpio_tt_bh_config_##_num = {                         \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(_num)},                \
		.ngpios = DT_INST_PROP(_num, ngpios),                                              \
		.trien_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), trien),                       \
		.rxen_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), rxen),                         \
		.data_addr = DT_REG_ADDR_BY_NAME(DT_DRV_INST(_num), data),                         \
	};                                                                                         \
                                                                                                   \
	static struct gpio_tt_bh_data gpio_tt_bh_data_##_num = {};                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_num, gpio_tt_bh_init, NULL, &gpio_tt_bh_data_##_num,                \
			      &gpio_tt_bh_config_##_num, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY,   \
			      &gpio_tt_bh_driver);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_GPIO_TT_BH)
