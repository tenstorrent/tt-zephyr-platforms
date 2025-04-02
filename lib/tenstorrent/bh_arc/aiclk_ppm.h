/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AICLK_PPM_H
#define AICLK_PPM_H

#include <stdint.h>

typedef enum {
	kAiclkArbMaxFmax,
	kAiclkArbMaxTDP,
	kAiclkArbMaxFastTDC,
	kAiclkArbMaxTDC,
	kAiclkArbMaxThm,
	kAiclkArbMaxBoardPwr,
	kAiclkArbMaxVoltage,
	kAiclkArbMaxGDDRThm,
	kAiclkArbMaxCount,
} AiclkArbMax;

typedef enum {
	kAiclkArbMinFmin,
	kAiclkArbMinBusy,
	kAiclkArbMinCount,
} AiclkArbMin;

typedef struct {
	uint32_t curr_freq;   /* in MHz */
	uint32_t targ_freq;   /* in MHz */
	uint32_t boot_freq;   /* in MHz */
	uint32_t fmax;        /* in MHz */
	uint32_t fmin;        /* in MHz */
	uint32_t forced_freq; /* in MHz, a value of zero means disabled. */
	float arbiter_max[kAiclkArbMaxCount];
	float arbiter_min[kAiclkArbMinCount];
} AiclkPPM;

extern AiclkPPM aiclk_ppm;

void SetAiclkArbMax(AiclkArbMax arb_max, float freq);
void SetAiclkArbMin(AiclkArbMin arb_min, float freq);
void CalculateTargAiclk(void);
void DecreaseAiclk(void);
void IncreaseAiclk(void);
void InitArbMaxVoltage(void);
void InitAiclkPPM(void);
uint8_t ForceAiclk(uint32_t freq);

#endif
