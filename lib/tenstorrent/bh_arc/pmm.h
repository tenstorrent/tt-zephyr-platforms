/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PMM_H
#define PMM_H

#include <stdint.h>

void MailboxWrite(uint8_t data, uint8_t busy, uint8_t flag0, uint8_t flag1, uint8_t flag2,
		  uint8_t flag3, uint8_t node_type, uint8_t y, uint8_t x);
void ClearPMMStatus(void);
void EnablePMM(void);
void DisablePMM(void);
inline void WritePMMReg(uint32_t dw_offset, uint32_t data);
inline uint32_t ReadPMMReg(uint32_t dw_offset);
#endif
