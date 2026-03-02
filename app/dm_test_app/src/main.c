/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

LOG_MODULE_REGISTER(dm_test_app, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Hello World! This is dm_test_app running on %s", CONFIG_BOARD);

	while (1) {
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
