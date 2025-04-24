/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <tenstorrent/bm_event.h>

static K_EVENT_DEFINE(bm_event);
LOG_MODULE_REGISTER(bm_event, CONFIG_TT_BM_EVENT_LOG_LEVEL);

uint32_t bm_event_post(uint32_t events)
{
	return k_event_post(&bm_event, events);
}

uint32_t bm_event_wait(uint32_t events, k_timeout_t timeout)
{
	uint32_t ret = k_event_wait(&bm_event, events, false, timeout);

	if (ret != 0) {
		LOG_INF("Event wait successful: requested=0x%08X, received=0x%08X", events, ret);
	}

	return ret;
}
