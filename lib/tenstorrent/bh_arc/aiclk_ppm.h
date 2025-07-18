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
	kAiclkArbMaxBoardPower,
	kAiclkArbMaxVoltage,
	kAiclkArbMaxGDDRThm,
	kAiclkArbMaxCount,
} AiclkArbMax;

typedef enum {
	kAiclkArbMinFmin,
	kAiclkArbMinBusy,
	kAiclkArbMinCount,
} AiclkArbMin;

void SetAiclkArbMax(AiclkArbMax arb_max, float freq);
void SetAiclkArbMin(AiclkArbMin arb_min, float freq);
void CalculateTargAiclk(void);
void DecreaseAiclk(void);
void IncreaseAiclk(void);
void InitArbMaxVoltage(void);
float GetThrottlerArbMax(AiclkArbMax arb_max);
uint8_t ForceAiclk(uint32_t freq);
uint32_t GetAiclkTarg(void);

#endif
