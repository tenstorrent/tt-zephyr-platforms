/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "asic_state.h"

#include <zephyr/kernel.h>
#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>

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
#if !(defined(CONFIG_TT_SMC_RECOVERY) || defined(CONFIG_BH_FWTABLE))
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

static uint8_t asic_state_handler(const union request *request, struct response *response)
{
	if (request->command_code == TT_SMC_MSG_ASIC_STATE0) {
		enter_state0();
	} else if (request->command_code == TT_SMC_MSG_ASIC_STATE3) {
		enter_state3();
	}
	return 0;
}

void set_asic_state(AsicState state)
{
	if (state == A3State) {
		enter_state3();
	} else {
		enter_state0();
	}
}

AsicState get_asic_state(void)
{
	return asic_state;
}

REGISTER_MESSAGE(TT_SMC_MSG_ASIC_STATE0, asic_state_handler);
REGISTER_MESSAGE(TT_SMC_MSG_ASIC_STATE3, asic_state_handler);
