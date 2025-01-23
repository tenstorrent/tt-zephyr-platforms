/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "asic_state.h"

#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "pll.h"
#include "regulator.h"
#include "aiclk_ppm.h"
#include "voltage.h"

uint8_t asic_state = A0State;

static void enter_state0(void)
{
	asic_state = A0State;
}

static void enter_state3(void)
{
#ifndef CONFIG_TT_SMC_RECOVERY
	ForceAiclk(800);
	ForceVdd(750);
#endif
	asic_state = A3State;
}

/* May be called from ISR. */
void lock_down_for_reset(void)
{
	asic_state = A3State;

	/* More could be done here. We can shut down everything except the SMBus slave */
	/* (and the I2C code it relies on). */
}

static uint8_t asic_state_handler(uint32_t msg_code, const struct request *request,
				  struct response *response)
{
	if (msg_code == MSG_TYPE_ASIC_STATE0) {
		enter_state0();
	} else if (msg_code == MSG_TYPE_ASIC_STATE3) {
		enter_state3();
	}
	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_ASIC_STATE0, asic_state_handler);
REGISTER_MESSAGE(MSG_TYPE_ASIC_STATE3, asic_state_handler);
