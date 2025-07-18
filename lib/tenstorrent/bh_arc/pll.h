/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PLL_H
#define PLL_H

#include <stdint.h>

int PLLInit(void);
void PLLAllBypass(void);
uint32_t GetAICLK(void);
uint32_t GetAPBCLK(void);
uint32_t GetAXICLK(void);
uint32_t GetARCCLK(void);
uint32_t GetL2CPUCLK(uint8_t l2cpu_num);
int SetGddrMemClk(uint32_t gddr_mem_clk_mhz);
void SetAICLK(uint32_t aiclk_in_mhz);
void DropAICLK(void);
#endif
