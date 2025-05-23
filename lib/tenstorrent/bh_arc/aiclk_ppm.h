/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AICLK_PPM_H
#define AICLK_PPM_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
	kAiclkArbMaxFmax,
	kAiclkArbMaxTDP,
	kAiclkArbMaxFastTDC,
	kAiclkArbMaxTDC,
	kAiclkArbMaxThm,
	kAiclkArbMaxBoardPower,
	kAiclkArbMaxVoltage,
	kAiclkArbMaxGDDRThm,
	kAiclkArbMaxDopplerSlow,
	kAiclkArbMaxDopplerCritical,
	kAiclkArbMaxCount,
} AiclkArbMax;

typedef enum {
	kAiclkArbMinFmin,
	kAiclkArbMinBusy,
	kAiclkArbMinCount,
} AiclkArbMin;

void aiclk_set_busy(bool is_busy);
void SetAiclkArbMax(AiclkArbMax arb_max, float freq);
void SetAiclkArbMin(AiclkArbMin arb_min, float freq);
void EnableArbMax(AiclkArbMax arb_max, bool enable);
void EnableArbMin(AiclkArbMin arb_min, bool enable);
void CalculateTargAiclk(void);
void DecreaseAiclk(void);
void IncreaseAiclk(void);
void InitArbMaxVoltage(void);
float GetThrottlerArbMax(AiclkArbMax arb_max);
uint8_t ForceAiclk(uint32_t freq);
uint32_t GetAiclkTarg(void);
uint32_t GetMaxAiclkForVoltage(uint32_t voltage);
uint32_t GetAiclkFmin(void);
uint32_t GetAiclkFmax(void);

#endif
