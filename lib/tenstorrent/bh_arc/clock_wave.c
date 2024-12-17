/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "clock_wave.h"

#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "timer.h"
#include "reg.h"

#define PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR 0x80020038

typedef struct {
	uint32_t aiclk_zsk_enb: 1;
	uint32_t aiclk_mesh_enb: 1;
} PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_reg_t;

typedef union {
	uint32_t val;
	PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_reg_t f;
} PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_reg_u;

#define PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_DEFAULT 0x00000001

void SwitchClkScheme(ClockingScheme clk_scheme)
{
	PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_reg_u clock_wave_cntl;

	clock_wave_cntl.val = 0;
	WriteReg(PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR, clock_wave_cntl.val);
	Wait(10); /* both enables are off for 10 refclk cycles */
	if (clk_scheme == ClockWave) {
		clock_wave_cntl.f.aiclk_mesh_enb = 1;
		clock_wave_cntl.f.aiclk_zsk_enb = 0;
	} else {
		clock_wave_cntl.f.aiclk_mesh_enb = 0;
		clock_wave_cntl.f.aiclk_zsk_enb = 1;
	}
	WriteReg(PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR, clock_wave_cntl.val);
	Wait(10); /* wait for 10 refclk cycles for aiclk to stablize */
}

uint8_t switch_clk_scheme_handler(uint32_t msg_code, const struct request *request,
				  struct response *response)
{
	uint32_t clk_scheme = request->data[1];

	SwitchClkScheme(clk_scheme);
	return 0;
}
REGISTER_MESSAGE(MSG_TYPE_SWITCH_CLK_SCHEME, switch_clk_scheme_handler);
