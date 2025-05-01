/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_LIB_DM_EVENT_H_
#define INCLUDE_TENSTORRENT_LIB_DM_EVENT_H_

#include <stdint.h>
#include <zephyr/kernel.h>

#define WAKE_DM_MAIN_LOOP 1

uint32_t dm_event_post(uint32_t events);
uint32_t dm_event_wait(uint32_t events, k_timeout_t timeout);

#endif
