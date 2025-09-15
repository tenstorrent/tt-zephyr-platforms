/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>

#include "telemetry.h"
#include "smbus_target.h"
#include "asic_state.h"
LOG_MODULE_REGISTER(tt_shell, CONFIG_LOG_DEFAULT_LEVEL);

int asic_state_handler(const struct shell *sh, size_t argc, char **argv)
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

int telem_handler(const struct shell *sh, size_t argc, char **argv)
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

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tt_commands,
			       SHELL_CMD_ARG(asic_state, NULL, "[|0|3]", asic_state_handler, 1, 1),
			       SHELL_CMD_ARG(telem, NULL, "<Telemetry Index> [|x|f|d]",
					     telem_handler, 2, 1),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tt, &sub_tt_commands, "Tensorrent commands", NULL);
