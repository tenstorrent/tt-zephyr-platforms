/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AVS_H
#define AVS_H

#include <stdint.h>

typedef enum {
	AVSOk = 0,
	AVSResourceUnavailable = 1, /* retry */
	AVSBadCrc = 2,              /* retry */
	AVSGoodCrcBadData = 3,      /* no retry */
} AVSStatus;

typedef enum {
	AVSPwrModeMaxEff = 0,
	AVSPwrModeMaxPower = 3,
} AVSPwrMode;

#define AVS_VCORE_RAIL  0
#define AVS_VCOREM_RAIL 1

AVSStatus AVSReadVoltage(uint8_t rail_sel, uint16_t *voltage_in_mV);
AVSStatus AVSWriteVoltage(uint16_t voltage_in_mV, uint8_t rail_sel);
AVSStatus AVSReadVoutTransRate(uint8_t rail_sel, uint8_t *rise_rate, uint8_t *fall_rate);
AVSStatus AVSWriteVoutTransRate(uint8_t rise_rate, uint8_t fall_rate, uint8_t rail_sel);
AVSStatus AVSReadCurrent(uint8_t rail_sel, float *current_in_A);
AVSStatus AVSReadTemp(uint8_t rail_sel, float *temp_in_C);
AVSStatus AVSForceVoltageReset(uint8_t rail_sel);
AVSStatus AVSReadPowerMode(uint8_t rail_sel, AVSPwrMode *power_mode);
AVSStatus AVSWritePowerMode(AVSPwrMode power_mode, uint8_t rail_sel);
AVSStatus AVSReadStatus(uint8_t rail_sel, uint16_t *status);
AVSStatus AVSWriteStatus(uint16_t status, uint8_t rail_sel);
AVSStatus AVSReadVersion(uint16_t *version);
AVSStatus AVSReadSystemInputCurrent(uint16_t *response);
#endif
