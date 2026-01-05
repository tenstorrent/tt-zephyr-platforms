/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AICLK_PPM_H
#define AICLK_PPM_H

#include <stdint.h>
#include <stdbool.h>

enum aiclk_arb_max {
	aiclk_arb_max_fmax,
	aiclk_arb_max_tdp,
	aiclk_arb_max_fast_tdc,
	aiclk_arb_max_tdc,
	aiclk_arb_max_thm,
	aiclk_arb_max_board_power,
	aiclk_arb_max_voltage,
	aiclk_arb_max_gddr_thm,
	aiclk_arb_max_doppler_slow,
	aiclk_arb_max_doppler_critical,
	aiclk_arb_max_count,
};

enum aiclk_arb_min {
	aiclk_arb_min_fmin,
	aiclk_arb_min_busy,
	aiclk_arb_min_count,
};

void aiclk_update_busy(void);
void SetAiclkArbMax(enum aiclk_arb_max arb_max, float freq);
void SetAiclkArbMin(enum aiclk_arb_min arb_min, float freq);
void EnableArbMax(enum aiclk_arb_max arb_max, bool enable);
void EnableArbMin(enum aiclk_arb_min arb_min, bool enable);
void CalculateTargAiclk(void);
void DecreaseAiclk(void);
void IncreaseAiclk(void);
void InitArbMaxVoltage(void);
float GetThrottlerArbMax(enum aiclk_arb_max arb_max);
uint8_t ForceAiclk(uint32_t freq);
uint32_t GetAiclkTarg(void);
uint32_t GetMaxAiclkForVoltage(uint32_t voltage);
uint32_t GetAiclkFmin(void);
uint32_t GetAiclkFmax(void);
uint32_t get_aiclk_effective_arb_min(void);
uint32_t get_aiclk_effective_arb_max(void);
uint32_t get_enabled_arb_min_bitmask(void);
uint32_t get_enabled_arb_max_bitmask(void);

#endif
