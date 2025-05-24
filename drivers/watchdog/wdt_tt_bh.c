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

struct wdt_tt_bh_config {
	uint32_t timeout_ms;
};

struct wdt_tt_bh_data {
	uint32_t heartbeat;
};

static int wdt_tt_bh_disable(const struct device *dev)
{
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdAutoResetTimeoutUpdate, .data = 0 /* in ms */
	};
	EnqueueCm2DmMsg(&msg);

	return 0;
}

static int wdt_tt_bh_setup(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(options);

	return 0;
}

static int wdt_tt_bh_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdAutoResetTimeoutUpdate, .data = cfg->window.min /* in ms */
	};
	EnqueueCm2DmMsg(&msg);

	return 0;
}

static int wdt_tt_bh_feed(const struct device *dev, int channel_id)
{

	struct wdt_tt_bh_data *data = dev->data;

	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgTelemHeartbeatUpdate, .data = data->heartbeat++, /* in ms */
	};

	__ASSERT(channel_id == 0, "Invalid channel ID: %d", channel_id);

	EnqueueCm2DmMsg(&msg);

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
	const struct wdt_tt_bh_config *config = dev->config;
	const struct wdt_timeout_cfg cfg = {.window = {
						    .min = config->timeout_ms,
						    .max = config->timeout_ms,
					    }};

	if (IS_ENABLED(CONFIG_WDT_DISABLE_AT_BOOT)) {
		wdt_tt_bh_disable(dev);

		return 0;
	}
	wdt_tt_bh_install_timeout(dev, &cfg);

	return 0;
}

#define WDT_TT_BH_DRIVER_INIT(inst)                                                                \
	static struct wdt_tt_bh_data wdt_tt_bh_data_##inst;                                        \
	static const struct wdt_tt_bh_config wdt_tt_bh_config_##inst = {                           \
		.timeout_ms = DT_INST_PROP(inst, timeout_ms),                                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, wdt_tt_bh_init, NULL, &wdt_tt_bh_data_##inst,                  \
			      &wdt_tt_bh_config_##inst, PRE_KERNEL_1,                              \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &wdt_tt_bh_api);

DT_INST_FOREACH_STATUS_OKAY(WDT_TT_BH_DRIVER_INIT)
