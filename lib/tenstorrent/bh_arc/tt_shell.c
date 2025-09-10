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
LOG_MODULE_REGISTER(tt_shell, CONFIG_LOG_DEFAULT_LEVEL);

int telem_handler(const struct shell *sh, size_t argc, char **argv)
{
	int32_t idx = atoi(argv[1]);

	if (!GetTelemetryTagValid(idx)) {
		shell_error(sh, "Invalid telemetry tag");
		return -EINVAL;
	}

	shell_print(sh, "0x%08X", GetTelemetryTag(idx));
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_tt_commands,
			       SHELL_CMD_ARG(telem, NULL, "<Telemetry Index>", telem_handler, 2, 0),
			       SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(tt, &sub_tt_commands, "Tensorrent commands", NULL);
