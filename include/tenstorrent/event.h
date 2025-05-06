/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_LIB_EVENT_H_
#define INCLUDE_TENSTORRENT_LIB_EVENT_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event IDs for Tenstorrent firmware.
 *
 * The application thread of firmware can receive and react to events generated throughout the
 * system. Multiple events may be posted and receieved simultaneously, as they form a bitmask.
 */
enum tt_event {
	TT_EVENT_WAKE = BIT(31), /**< @brief Wake firmware for a generic reason */
};

/** @brief Bitmask of all Tenstorrent firmware events */
#define TT_EVENT_MASK (TT_EVENT_WAKE)

/**
 * @brief Post an event to Tenstorrent firmware.
 *
 * Post one or more @a events.
 *
 * @funcprops \isr_ok
 *
 * @param events The events to post as a bitmask of @ref tt_event values.
 * @return the previous value of posted events.
 *
 * @see k_event_post
 */
uint32_t tt_event_post(uint32_t events);

/**
 * @brief Wait for one or more events to be posted to Tenstorrent firmware.
 *
 * Wait for one or more @a events to be posted to Tenstorrent firmware. The function will
 * block until at least one of the specified events are received or @a timeout expires.
 *
 * To block indefinitely, use @ref K_FOREVER. To return immediately, use @ref K_NO_WAIT.
 *
 * To block for a specific time, use @ref K_MSEC or @ref K_USEC to specify the timeout.
 *
 * On success, a bitmask of the received events (of type @ref tt_event) is returned and the
 * corresponding events are automatically cleared. When a timeout occurs, the function returns 0.
 *
 * @note This function may be called from ISR context only if @a timeout equals @ref K_NO_WAIT.
 *
 * @param events The events to wait for as a bitmask of @ref tt_event values.
 * @param timeout The maximum time to wait for the event(s) to be posted.
 *
 * @return On success, a bitmask of the received events (of type @ref tt_event). Otherwise, zero.
 *
 * @see k_event_wait
 */
uint32_t tt_event_wait(uint32_t events, k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif
