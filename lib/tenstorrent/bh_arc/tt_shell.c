/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include <tenstorrent/bh_power.h>

#include "telemetry.h"
#include "smbus_target.h"
#include "gddr.h"
#include "asic_state.h"
#include "noc_init.h"
LOG_MODULE_REGISTER(tt_shell, CONFIG_LOG_DEFAULT_LEVEL);

static int l2cpu_enable_handler(const struct shell *sh, size_t argc, char **argv)
{
	bool on = false;

	if (strcmp(argv[1], "off") == 0) {
		on = false;

	} else if (strcmp(argv[1], "on") == 0) {
		on = true;
	} else {
		shell_error(sh, "Invalid L2CPU power setting");

		return -EINVAL;
	}

	int ret = bh_set_l2cpu_enable(on);

	if (ret != 0) {
		shell_error(sh, "Failure to set L2CPU power setting %u", on);
		return ret;
	}
	shell_print(sh, "OK");
	return 0;
}

static int tensix_enable_handler(const struct shell *sh, size_t argc, char **argv)
{
	bool on = false;

	if (strcmp(argv[1], "off") == 0) {
		on = false;

	} else if (strcmp(argv[1], "on") == 0) {
		on = true;
	} else {
		shell_error(sh, "Invalid tensix power setting");

		return -EINVAL;
	}

	int ret = set_tensix_enable(on);

	if (ret != 0) {
		shell_error(sh, "Failure to set tensix power setting %u", on);
		return ret;
	}
	shell_print(sh, "OK");
	return 0;
}

static int mrisc_power_handler(const struct shell *sh, size_t argc, char **argv)
{
	bool on = false;

	if (strcmp(argv[1], "off") == 0) {
		on = false;

	} else if (strcmp(argv[1], "on") == 0) {
		on = true;
	} else {
		shell_error(sh, "Invalid MRISC power setting");

		return -EINVAL;
	}

	int ret = set_mrisc_power_setting(on);

	if (ret != 0) {
		shell_error(sh, "Failure to set MRISC power setting %u", on);
		return ret;
	}
	shell_print(sh, "OK");
	return 0;
}

static int asic_state_handler(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 2U) {
		AsicState state = (AsicState)atoi(argv[1]);

		if (state == A0State || state == A3State) {
			set_asic_state(state);
			shell_print(sh, "OK");
		} else {
			shell_error(sh, "Invalid ASIC State");
			return -EINVAL;
		}
	} else {
		shell_print(sh, "ASIC State: %u", get_asic_state());
	}

	return 0;
}

static int telem_handler(const struct shell *sh, size_t argc, char **argv)
{
	int32_t idx = atoi(argv[1]);
	char fmt;
	uint32_t value;

	if (argc == 3 && (strlen(argv[2]) != 1U)) {
		shell_error(sh, "Invalid format");
		return -EINVAL;
	}
	if (argc == 2) {
		fmt = 'x';
	} else {
		fmt = argv[2][0];
	}

	if (!GetTelemetryTagValid(idx)) {
		shell_error(sh, "Invalid telemetry tag");
		return -EINVAL;
	}

	value = GetTelemetryTag(idx);

	if (fmt == 'x') {
		shell_print(sh, "0x%08X", value);
	} else if (fmt == 'f') {
		shell_print(sh, "%lf", (double)ConvertTelemetryToFloat(value));
	} else if (fmt == 'd') {
		shell_print(sh, "%d", value);
	} else {
		shell_error(sh, "Invalid format");
		return -EINVAL;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_tt_commands, SHELL_CMD_ARG(mrisc_power, NULL, "[off|on]", mrisc_power_handler, 2, 0),
	SHELL_CMD_ARG(tensix_power, NULL, "[off|on]", tensix_enable_handler, 2, 0),
	SHELL_CMD_ARG(l2cpu_power, NULL, "[off|on]", l2cpu_enable_handler, 2, 0),
	SHELL_CMD_ARG(asic_state, NULL, "[|0|3]", asic_state_handler, 1, 1),
	SHELL_CMD_ARG(telem, NULL, "<Telemetry Index> [|x|f|d]", telem_handler, 2, 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tt, &sub_tt_commands, "Tenstorrent commands", NULL);
