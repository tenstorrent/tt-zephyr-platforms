/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AICLK_PPM_H
#define AICLK_PPM_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief AICLK maximum frequency arbiters
 *
 * These arbiters set upper limits on the AICLK frequency. The effective
 * maximum frequency is determined by the lowest enabled arbiter value.
 *
 * @warning The order of these enum values must be preserved for compatibility.
 */
enum aiclk_arb_max {
	aiclk_arb_max_fmax,              /**< Maximum frequency limit */
	aiclk_arb_max_tdp,               /**< Thermal Design Power limit */
	aiclk_arb_max_fast_tdc,          /**< Fast Thermal Design Current limit */
	aiclk_arb_max_tdc,               /**< Thermal Design Current limit */
	aiclk_arb_max_thm,               /**< Thermal limit */
	aiclk_arb_max_board_power,       /**< Board power limit */
	aiclk_arb_max_voltage,           /**< Voltage limit */
	aiclk_arb_max_gddr_thm,          /**< GDDR thermal limit */
	aiclk_arb_max_doppler_slow,      /**< Doppler slow throttling limit */
	aiclk_arb_max_doppler_critical,  /**< Doppler critical throttling limit */
	aiclk_arb_max_count,             /**< Number of max arbiters */
};

/**
 * @brief AICLK minimum frequency arbiters
 *
 * These arbiters set lower limits on the AICLK frequency. The effective
 * minimum frequency is determined by the highest enabled arbiter value.
 *
 * @warning The order of these enum values must be preserved for compatibility.
 */
enum aiclk_arb_min {
	aiclk_arb_min_fmin,   /**< Minimum frequency limit */
	aiclk_arb_min_busy,   /**< Busy state frequency requirement */
	aiclk_arb_min_count,  /**< Number of min arbiters */
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
