/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "voltage.h"
#include "regulator.h"
#include "dvfs.h"

/* TODO: Get these from SPI parameters */
#define VDD_MIN  750
#define VDD_MAX  900
#define VDD_BOOT 750

VoltageArbiter voltage_arbiter;

void VoltageChange(void)
{
	if (voltage_arbiter.targ_voltage != voltage_arbiter.curr_voltage) {
		set_vcore(voltage_arbiter.targ_voltage);
		voltage_arbiter.curr_voltage = voltage_arbiter.targ_voltage;
	}
}

void VoltageArbRequest(VoltageRequestor req, uint32_t voltage)
{
	voltage_arbiter.req_voltage[req] =
		CLAMP(voltage, voltage_arbiter.vdd_min, voltage_arbiter.vdd_max);
}

void CalculateTargVoltage(void)
{
	/* The target voltage is the maximum of all requested voltages */
	uint32_t targ_voltage = voltage_arbiter.vdd_min;

	for (VoltageRequestor i = 0; i < VoltageReqCount; i++) {
		if (voltage_arbiter.req_voltage[i] > targ_voltage) {
			targ_voltage = voltage_arbiter.req_voltage[i];
		}
	}

	/* Limit to vdd_max */
	voltage_arbiter.targ_voltage = MIN(targ_voltage, voltage_arbiter.vdd_max);

	/* Apply forced voltage at the end, regardless of any limits */
	if (voltage_arbiter.forced_voltage != 0) {
		voltage_arbiter.targ_voltage = voltage_arbiter.forced_voltage;
	}
}

int InitVoltagePPM(void)
{
	voltage_arbiter.vdd_min = VDD_MIN;
	voltage_arbiter.vdd_max = VDD_MAX;

	/* disable forcing of VDD */
	voltage_arbiter.forced_voltage = 0;

	for (VoltageRequestor i = 0; i < VoltageReqCount; i++) {
		voltage_arbiter.req_voltage[i] = voltage_arbiter.vdd_min;
	}
	set_vcore(VDD_BOOT);
	voltage_arbiter.curr_voltage = VDD_BOOT;
	voltage_arbiter.targ_voltage = voltage_arbiter.curr_voltage;

	/* Change VCOREM to 0.85 V to enforce the rule VCOREM - 300 mV <= VCORE <= VCOREM + 100mV */
	/* Thus allowing VCORE in the range of 0.55 V to 0.95 V */
	set_vcorem(850);

	return 0;
}

uint8_t ForceVdd(uint32_t voltage)
{
	if ((voltage > VDD_MAX || voltage < VDD_MIN) && (voltage != 0)) {
		return 1;
	}

	if (dvfs_enabled) {
		voltage_arbiter.forced_voltage = voltage;
		DVFSChange();
	} else {
		/* restore to boot voltage */
		if (voltage == 0) {
			voltage = VDD_BOOT;
		}

		set_vcore(voltage);
	}
	return 0;
}

static uint8_t ForceVddHandler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	uint32_t forced_voltage = request->data[1];

	return ForceVdd(forced_voltage);
}

REGISTER_MESSAGE(MSG_TYPE_FORCE_VDD, ForceVddHandler);
