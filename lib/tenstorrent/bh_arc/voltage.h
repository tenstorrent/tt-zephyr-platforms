/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VOLTAGE_H
#define VOLTAGE_H

#include <stdint.h>

typedef enum {
	VoltageReqAiclk,
	VoltageReqL2CPU,
	VoltageReqCount,
} VoltageRequestor;

typedef struct {
	uint32_t curr_voltage;                 /* in mV */
	uint32_t targ_voltage;                 /* in mV */
	uint32_t vdd_min;                      /* in mV */
	uint32_t vdd_max;                      /* in mV */
	uint32_t forced_voltage;               /* in mV, a value of zero means disabled. */
	uint32_t req_voltage[VoltageReqCount]; /* in mV */
} VoltageArbiter;

extern VoltageArbiter voltage_arbiter;

void VoltageChange(void);
void VoltageArbRequest(VoltageRequestor req, uint32_t voltage);
void CalculateTargVoltage(void);
int InitVoltagePPM(void);
uint8_t ForceVdd(uint32_t voltage);

#endif
