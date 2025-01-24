/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "regulator.h"

#include <math.h>  /* for ldexp */
#include <float.h> /* for FLT_MAX */
#include <stdint.h>
#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "timer.h"
#include "dw_apb_i2c.h"
#include "avs.h"

#define GDDR_VDD_RFB1    1.37
#define GDDR_VDD_RFB2    4.32
#define GDDRIO_RFB1      1.74
#define GDDRIO_RFB2      1.6
#define SERDES_VDD_RFB1  1.69
#define SERDES_VDD_RFB2  10
#define SERDES_VDDH_RFB1 1.74
#define SERDES_VDDH_RFB2 2.05

/* I2C constants */
#define PMBUS_MST_ID 1

/* PMBus Spec constants */
#define VOUT_COMMAND                0x21
#define VOUT_COMMAND_DATA_BYTE_SIZE 2
#define READ_VOUT                   0x8B
#define READ_VOUT_DATA_BYTE_SIZE    2
#define READ_IOUT                   0x8C
#define READ_IOUT_DATA_BYTE_SIZE    2
#define READ_POUT                   0x96
#define READ_POUT_DATA_BYTE_SIZE    2
#define OPERATION                   0x1
#define OPERATION_DATA_BYTE_SIZE    1
#define PMBUS_CMD_BYTE_SIZE         1
#define PMBUS_FLIP_BYTES            0

#define VCORE_RAIL_NUM  0
#define VCOREM_RAIL_NUM 1

typedef struct {
	uint8_t reserved: 1;
	uint8_t transition_control: 1;
	uint8_t margin_fault_response: 2;

	VoltageCmdSource voltage_command_source : 2;
	uint8_t turn_off_behaviour: 1;
	uint8_t on_off_state: 1;
} OperationBits;

/* The default value is the regulator default */
static uint8_t vout_cmd_source = VoutCommand;

/* vout = vref * (1 + rfb1/rfb2) */
static float ConvertVrefToVout(float vref, float rfb1, float rfb2)
{
	return vref * (1 + rfb1 / rfb2);
}

/* vref = (rfb2 * vout) / (rfb1 * rfb2) */
static float ConvertVoutToVref(float vout, float rfb1, float rfb2)
{
	return (rfb2 * vout) / (rfb1 + rfb2);
}

/* Refer to Table 8 of the MAXIM databook for details. */
/* Return value is in mV. */
static float ConvertVoutCmdToVref(uint16_t vout_cmd)
{
	return 3.906f * ((vout_cmd - 307) / 2) + 601.6f;
}

static uint16_t ConvertVrefToVoutCmd(float vref)
{
	return (vref - 601.6f) * 2 / 3.906f + 307;
}

static float ConvertLinear11ToFloat(uint16_t value)
{
	int16_t exponent = (value >> 11) & 0x1f;
	uint16_t mantissa = value & 0x7ff;

	if (exponent >> 4 == 1) { /* sign extension if negative */
		exponent |= ~0x1F;
	}

	return ldexp(mantissa, exponent);
}

static void GetRfbFromSlaveAddr(uint32_t slave_addr, float *rfb1, float *rfb2)
{
	if (slave_addr == P0V85_GDDR_VDDA_WEST_ADDR || slave_addr == P0V85_GDDR_VDDA_EAST_ADDR ||
	    slave_addr == P0V85_GDDR_VDDR_WEST_ADDR || slave_addr == P0V85_GDDR_VDDR_EAST_ADDR) {
		*rfb1 = GDDR_VDD_RFB1;
		*rfb2 = GDDR_VDD_RFB2;
	} else if (slave_addr == P1V35_GDDRIO_WEST || slave_addr == P1V35_GDDRIO_EAST) {
		*rfb1 = GDDRIO_RFB1;
		*rfb2 = GDDRIO_RFB2;
	} else if (slave_addr == P0V75_SERDES_VDD || slave_addr == P0V75_SERDES_VDDL) {
		*rfb1 = SERDES_VDD_RFB1;
		*rfb2 = SERDES_VDD_RFB2;
	} else if (slave_addr == P1V2_SERDES_VDDH) {
		*rfb1 = SERDES_VDDH_RFB1;
		*rfb2 = SERDES_VDDH_RFB2;
	} else {
		/* the goal here is to get vout = vref / 2 */
		*rfb1 = -1.0;
		*rfb2 = 2.0;
	}
}

/* The function returns the core current in A. */
float GetVcoreCurrent(void)
{
	I2CInit(I2CMst, P0V8_VCORE_ADDR, I2CFastMode, PMBUS_MST_ID);
	uint16_t iout;

	I2CReadBytes(PMBUS_MST_ID, READ_IOUT, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&iout,
		     READ_IOUT_DATA_BYTE_SIZE, PMBUS_FLIP_BYTES);
	return ConvertLinear11ToFloat(iout);
}

/* The function returns the core power in W. */
float GetVcorePower(void)
{
	I2CInit(I2CMst, P0V8_VCORE_ADDR, I2CFastMode, PMBUS_MST_ID);
	uint16_t pout;

	I2CReadBytes(PMBUS_MST_ID, READ_POUT, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&pout,
		     READ_POUT_DATA_BYTE_SIZE, PMBUS_FLIP_BYTES);
	return ConvertLinear11ToFloat(pout);
}

/* The function returns the volage in mV. */
float GetVoltage(uint32_t slave_addr)
{
	/* TODO: set I2C mode depending on the maximum speed the device can support */
	I2CInit(I2CMst, slave_addr, I2CFastMode, PMBUS_MST_ID);
	uint16_t vout_cmd = 0;

	I2CReadBytes(PMBUS_MST_ID, READ_VOUT, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&vout_cmd,
		     READ_VOUT_DATA_BYTE_SIZE, PMBUS_FLIP_BYTES);

	float vref = vout_cmd;

	if (slave_addr != P0V8_VCORE_ADDR && slave_addr != P0V8_VCOREM_ADDR) {
		vref = ConvertVoutCmdToVref(vout_cmd);
	}

	float rfb1;
	float rfb2;

	GetRfbFromSlaveAddr(slave_addr, &rfb1, &rfb2);
	return ConvertVrefToVout(vref, rfb1, rfb2);
}

void I2CSetVoltage(uint32_t slave_addr, float voltage_in_mv)
{
	I2CInit(I2CMst, slave_addr, I2CFastMode, PMBUS_MST_ID);
	float rfb1;
	float rfb2;

	GetRfbFromSlaveAddr(slave_addr, &rfb1, &rfb2);
	float vref = ConvertVoutToVref(voltage_in_mv, rfb1, rfb2);

	uint16_t vout_cmd = vref;

	if (slave_addr != P0V8_VCORE_ADDR && slave_addr != P0V8_VCOREM_ADDR) {
		vout_cmd = ConvertVrefToVoutCmd(vref);
	}

	I2CWriteBytes(PMBUS_MST_ID, VOUT_COMMAND, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&vout_cmd,
		      VOUT_COMMAND_DATA_BYTE_SIZE);

	/* 100us to flush the tx of i2c + 150us to cover voltage switch from 0.65V to 0.95V with
	 * 50us of margin
	 */
	WaitUs(250);
}

void SetVoltage(uint32_t slave_addr, float voltage_in_mv)
{
	if (slave_addr == P0V8_VCORE_ADDR && vout_cmd_source == AVSVoutCommand) {
		AVSWriteVoltage(voltage_in_mv, AVS_VCORE_RAIL);
	} else {
		I2CSetVoltage(slave_addr, voltage_in_mv);
	}
}

void SwitchVoutControl(VoltageCmdSource source)
{
	I2CInit(I2CMst, P0V8_VCORE_ADDR, I2CFastMode, PMBUS_MST_ID);
	OperationBits operation;

	I2CReadBytes(PMBUS_MST_ID, OPERATION, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&operation,
		     OPERATION_DATA_BYTE_SIZE, PMBUS_FLIP_BYTES);
	operation.transition_control =
		1; /* copy vout command when control is passed from AVSBus to PMBus */
	operation.voltage_command_source = source;
	I2CWriteBytes(PMBUS_MST_ID, OPERATION, PMBUS_CMD_BYTE_SIZE, (uint8_t *)&operation,
		      OPERATION_DATA_BYTE_SIZE);

	/* 100us to flush the tx of i2c */
	WaitUs(100);
	vout_cmd_source = source;
}

static uint8_t set_voltage_handler(uint32_t msg_code, const struct request *request,
				   struct response *response)
{
	uint32_t slave_addr = request->data[1];
	float voltage_in_mv = request->data[2];

	SetVoltage(slave_addr, voltage_in_mv);
	return 0;
}

static uint8_t get_voltage_handler(uint32_t msg_code, const struct request *request,
				   struct response *response)
{
	uint32_t slave_addr = request->data[1];

	response->data[1] = GetVoltage(slave_addr);
	return 0;
}

static uint8_t switch_vout_control_handler(uint32_t msg_code, const struct request *request,
					   struct response *response)
{
	uint32_t source = request->data[1];

	SwitchVoutControl(source);
	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_SET_VOLTAGE, set_voltage_handler);
REGISTER_MESSAGE(MSG_TYPE_GET_VOLTAGE, get_voltage_handler);
REGISTER_MESSAGE(MSG_TYPE_SWITCH_VOUT_CONTROL, switch_vout_control_handler);
