/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_watchdog

#include <zephyr/kernel.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

#include "cm2dm_msg.h"

LOG_MODULE_REGISTER(wdt_tt_bh, CONFIG_WDT_LOG_LEVEL);

struct wdt_tt_bh_data {
	uint32_t heartbeat;
	uint32_t timeout;
};

static int wdt_tt_bh_disable(const struct device *dev)
{
	PostCm2DmMsg(kCm2DmMsgIdAutoResetTimeoutUpdate, 0);

	return 0;
}

static int wdt_tt_bh_setup(const struct device *dev, uint8_t options)
{
	struct wdt_tt_bh_data *data = dev->data;

	PostCm2DmMsg(kCm2DmMsgIdAutoResetTimeoutUpdate, data->timeout); /* in ms */

	return 0;
}

static int wdt_tt_bh_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	struct wdt_tt_bh_data *data = dev->data;

	data->timeout = cfg->window.max;
	return 0;
}

static int wdt_tt_bh_feed(const struct device *dev, int channel_id)
{

	struct wdt_tt_bh_data *data = dev->data;

	__ASSERT(channel_id == 0, "Invalid channel ID: %d", channel_id);

	PostCm2DmMsg(kCm2DmMsgTelemHeartbeatUpdate, data->heartbeat++);

	return 0;
}

static DEVICE_API(wdt, wdt_tt_bh_api) = {
	.setup = wdt_tt_bh_setup,
	.disable = wdt_tt_bh_disable,
	.install_timeout = wdt_tt_bh_install_timeout,
	.feed = wdt_tt_bh_feed,
};

static int wdt_tt_bh_init(const struct device *dev)
{
	/* Make sure watchdog is disabled at boot */
	wdt_tt_bh_disable(dev);
	return 0;
}

#define WDT_TT_BH_DRIVER_INIT(inst)                                                                \
	static struct wdt_tt_bh_data wdt_tt_bh_data_##inst;                                        \
	DEVICE_DT_INST_DEFINE(inst, wdt_tt_bh_init, NULL, &wdt_tt_bh_data_##inst, NULL,            \
			      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_tt_bh_api);

DT_INST_FOREACH_STATUS_OKAY(WDT_TT_BH_DRIVER_INIT)
