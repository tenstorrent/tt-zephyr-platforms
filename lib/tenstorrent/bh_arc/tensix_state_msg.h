/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSIX_STATE_MSG_H
#define TENSIX_STATE_MSG_H

#include <zephyr/zbus/zbus.h>

/* Transmitted on tensix_state_chan whenever the Tensixes are powered on or off. */
struct tensix_state_msg {
	bool enable;
};

ZBUS_CHAN_DECLARE(tensix_state_chan);

#endif
