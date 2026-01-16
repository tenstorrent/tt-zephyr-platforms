/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/gpio.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(tt_shell, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_NODE_HAS_COMPAT(DT_ROOT, tenstorrent_blackhole_p300)
int scandump(const struct shell *sh, size_t argc, char **argv)
{
	extern bool skip_evt_loop;
	const struct gpio_dt_spec dft_tap_sel =
		GPIO_DT_SPEC_GET(DT_CHILD(DT_NODELABEL(chip0), dft_tap_sel), gpios);
	const struct gpio_dt_spec dft_test_mode =
		GPIO_DT_SPEC_GET(DT_CHILD(DT_NODELABEL(chip0), dft_test_mode), gpios);

	const struct device *gpiox1 = DEVICE_DT_GET(DT_NODELABEL(gpiox1));
	const struct device *gpiox2 = DEVICE_DT_GET(DT_NODELABEL(gpiox2));
	if (strcmp(argv[1], "off") == 0) {
		shell_info(sh, "Turning scan dump mode off...");

		/* from GPIO expanders */
		for (int i = 0; i < 16; i++) {
			gpio_pin_configure(gpiox1, i, GPIO_OUTPUT_INACTIVE);
			gpio_pin_configure(gpiox2, i, GPIO_OUTPUT_INACTIVE);
		}

		/* directly connected to STM32 */
		gpio_pin_set_dt(&dft_tap_sel, 0);
		gpio_pin_set_dt(&dft_test_mode, 0);
		gpio_pin_configure_dt(&dft_tap_sel, GPIO_OUTPUT_INACTIVE);
		gpio_pin_configure_dt(&dft_test_mode, GPIO_OUTPUT_INACTIVE);
		skip_evt_loop = false;
		shell_info(sh, "Done!");
	} else if (strcmp(argv[1], "on") == 0) {
		shell_info(sh, "Turning scan dump mode on...");
		skip_evt_loop = true;
		/* directly connected to STM32 */
		gpio_pin_configure_dt(&dft_tap_sel, GPIO_OUTPUT_HIGH);
		gpio_pin_configure_dt(&dft_test_mode, GPIO_OUTPUT_HIGH);

		k_busy_wait(100);

		for (int i = 0; i < 16; i++) {
			gpio_pin_configure(gpiox1, i, GPIO_OUTPUT_LOW);
			gpio_pin_configure(gpiox2, i, GPIO_OUTPUT_LOW);
		}
		shell_info(sh, "Done!");
	} else {
		shell_error(sh, "Invalid MRISC power setting");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tt_commands,
			       SHELL_CMD_ARG(scandump, NULL, "[off|on]", scandump, 2, 0),
			       SHELL_SUBCMD_SET_END);
#else
SHELL_STATIC_SUBCMD_SET_CREATE(sub_tt_commands,
			       SHELL_SUBCMD_SET_END);
#endif /* !DT_NODE_HAS_COMPAT(DT_ROOT, tenstorrent_blackhole_p300) */

SHELL_CMD_REGISTER(tt, &sub_tt_commands, "Tenstorrent commands", NULL);
