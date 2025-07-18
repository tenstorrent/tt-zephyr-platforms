/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REGULATOR_H
#define REGULATOR_H

#include <stdint.h>
#include <zephyr/drivers/misc/bh_fwtable.h>

typedef enum {
	VoutCommand = 0,
	VoutMarginLow = 1,
	VoutMarginHigh = 2,
	AVSVoutCommand = 3,
} VoltageCmdSource;

uint32_t get_vcore(void);  /* returns voltage in mV. */
uint32_t get_vcorem(void); /* returns voltage in mV. */
void set_vcore(uint32_t voltage_in_mv);
void set_vcorem(uint32_t voltage_in_mv);
void set_gddr_vddr(PcbType board_type, uint32_t voltage_in_mv);
float GetVcoreCurrent(void);
float GetVcorePower(void);
void SwitchVoutControl(VoltageCmdSource source);
#endif
