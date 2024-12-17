/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PVT_H
#define PVT_H

#include <stdint.h>

#define NUM_TS 8
#define NUM_VM 8
#define NUM_PD 16

typedef enum {
	ReadOk = 0,
	SampleFault = 1,
	IncorrectSampleType = 2,
} ReadStatus;

void CATInit(void);
void PVTInit(void);
ReadStatus ReadTS(uint32_t id, uint16_t *data);
ReadStatus ReadVM(uint32_t id, uint16_t *data);
ReadStatus ReadPD(uint32_t id, uint16_t *data);
float GetAvgChipTemp(void);
#endif
