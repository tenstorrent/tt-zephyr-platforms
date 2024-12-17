/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tenstorrent/fwupdate.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

static void on_button_press(const struct device *port, struct gpio_callback *cb,
			    gpio_port_pins_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	tt_fwupdate("bmfw", false, true);
}

int main(void)
{
	int rc;
	static struct gpio_callback cb;
	static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(DT_ALIAS(sw0), gpios, {0});

	/* without this, the bootloader assumes that the firmware upgrade did not work */
	tt_fwupdate_confirm();

#ifdef CONFIG_TT_FWUPDATE_TEST
	rc = tt_fwupdate_create_test_fs("bmfw");
	if (rc < 0) {
		printk("tt_fwupdate_create_test_fs() failed: %d\n", rc);
		return EXIT_FAILURE;
	}
#endif

	if (!IS_ENABLED(CONFIG_GPIO)) {
		on_button_press(NULL, NULL, 0);
		return EXIT_SUCCESS;
	}

	if (!gpio_is_ready_dt(&button)) {
		printk("Button device %s not ready\n", button.port->name);
		return EXIT_FAILURE;
	}

	rc = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (rc < 0) {
		printk("gpio_pin_configure_dt() failed: %d\n", rc);
		return EXIT_FAILURE;
	}

	rc = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (rc < 0) {
		printk("gpio_pin_interrupt_configure_dt() failed: %d\n", rc);
		return EXIT_FAILURE;
	}

	gpio_init_callback(&cb, on_button_press, BIT(button.pin));
	rc = gpio_add_callback(button.port, &cb);
	if (rc < 0) {
		printk("gpio_add_callback() failed: %d\n", rc);
		return EXIT_FAILURE;
	}

	printk("waiting for button press...\n");
	while (true) {
		k_msleep(1000);
	}

	return EXIT_SUCCESS;
}
