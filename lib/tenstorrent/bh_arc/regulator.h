/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REGULATOR_H
#define REGULATOR_H

#include <stdint.h>

#define P0V8_VCORE_ADDR           0x64
#define P0V8_VCOREM_ADDR          0x65
#define P0V85_GDDR_VDDA_WEST_ADDR 0x50
#define P0V85_GDDR_VDDA_EAST_ADDR 0x51
#define P0V85_GDDR_VDDR_WEST_ADDR 0x54 
#define P0V85_GDDR_VDDR_EAST_ADDR 0x55
#define P1V35_GDDRIO_WEST         0x52
#define P1V35_GDDRIO_EAST         0x53
#define P1V2_SERDES_VDDH          0x58
#define P0V75_SERDES_VDD          0x56
#define P0V75_SERDES_VDDL         0x57

typedef enum {
  VoutCommand    = 0,
  VoutMarginLow  = 1,
  VoutMarginHigh = 2,
  AVSVoutCommand = 3,
} VoltageCmdSource;

float GetVoltage(uint32_t slave_addr); // returns voltage in mV.
void SetVoltage(uint32_t slave_addr, float voltage_in_mv);
float GetVcoreCurrent();
float GetVcorePower();
void SwitchVoutControl(VoltageCmdSource source);
#endif