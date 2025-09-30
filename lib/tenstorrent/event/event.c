/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <tenstorrent/event.h>

static K_EVENT_DEFINE(tt_event);
LOG_MODULE_REGISTER(tt_event, CONFIG_TT_EVENT_LOG_LEVEL);

uint32_t tt_event_post(uint32_t events)
{
	return k_event_post(&tt_event, events);
}

uint32_t tt_event_wait(uint32_t events, k_timeout_t timeout)
{
	uint32_t ret;

	ret = k_event_wait_safe(&tt_event, events, timeout);
	if (ret != 0) {
		LOG_INF("Received wake up event: requested=0x%08X received=0x%08X", events, ret);
	}

	return ret;
}
