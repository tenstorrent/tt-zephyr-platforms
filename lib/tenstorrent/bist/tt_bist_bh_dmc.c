/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include <tenstorrent/bist.h>
#include <tenstorrent/bh_chip.h>

LOG_MODULE_REGISTER(tt_bist, CONFIG_TT_BIST_LOG_LEVEL);

int tt_bist(void)
{
	const struct device *flash = BH_CHIPS[BH_CHIP_PRIMARY_INDEX].config.flash;

	if (!device_is_ready(flash)) {
		LOG_ERR("Flash device %s is not ready; continuing will mean app can't update\n",
			flash->name);
		return -1;
	}

	return 0;
}
