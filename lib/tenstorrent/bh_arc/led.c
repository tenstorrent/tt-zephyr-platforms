/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>

#include "cm2dm_msg.h"

/**
 * @brief Handler for @ref TT_SMC_MSG_BLINKY messages
 *
 * @details Blinks the red led.
 *
 * @param request Pointer to the host request message, use request->blink for
 *                structured access
 *
 * @retval 0 On success
 */
static uint8_t toggle_blinky_handler(const union request *request, struct response *response)
{
	RequestLedBlink(request->blink.is_blinking);
	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_BLINKY, toggle_blinky_handler);
