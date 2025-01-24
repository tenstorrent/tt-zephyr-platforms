/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "reg.h"

#define GPIOS_PER_REG 16

#define RESET_UNIT_GPIO_PAD_TRIEN_CNTL_REG_ADDR  0x800301A0
#define RESET_UNIT_GPIO2_PAD_TRIEN_CNTL_REG_ADDR 0x80030240
#define RESET_UNIT_GPIO3_PAD_TRIEN_CNTL_REG_ADDR 0x80030580
#define RESET_UNIT_GPIO4_PAD_TRIEN_CNTL_REG_ADDR 0x800305A0
#define RESET_UNIT_GPIO_PAD_RXEN_CNTL_REG_ADDR   0x800301AC
#define RESET_UNIT_GPIO2_PAD_RXEN_CNTL_REG_ADDR  0x8003025C
#define RESET_UNIT_GPIO3_PAD_RXEN_CNTL_REG_ADDR  0x8003058C
#define RESET_UNIT_GPIO4_PAD_RXEN_CNTL_REG_ADDR  0x800305AC
#define RESET_UNIT_GPIO_PAD_DATA_REG_ADDR        0x800301B4
#define RESET_UNIT_GPIO2_PAD_DATA_REG_ADDR       0x80030254
#define RESET_UNIT_GPIO3_PAD_DATA_REG_ADDR       0x80030594
#define RESET_UNIT_GPIO4_PAD_DATA_REG_ADDR       0x800305B4

static inline uint32_t GetTrienAddress(uint32_t id)
{
	if (id < GPIOS_PER_REG) {
		return RESET_UNIT_GPIO_PAD_TRIEN_CNTL_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 2) {
		return RESET_UNIT_GPIO2_PAD_TRIEN_CNTL_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 3) {
		return RESET_UNIT_GPIO3_PAD_TRIEN_CNTL_REG_ADDR;
	} else {
		return RESET_UNIT_GPIO4_PAD_TRIEN_CNTL_REG_ADDR;
	}
}

static inline uint32_t GetRxenAddress(uint32_t id)
{
	if (id < GPIOS_PER_REG) {
		return RESET_UNIT_GPIO_PAD_RXEN_CNTL_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 2) {
		return RESET_UNIT_GPIO2_PAD_RXEN_CNTL_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 3) {
		return RESET_UNIT_GPIO3_PAD_RXEN_CNTL_REG_ADDR;
	} else {
		return RESET_UNIT_GPIO4_PAD_RXEN_CNTL_REG_ADDR;
	}
}

static inline uint32_t GetPadDataAddress(uint32_t id)
{
	if (id < GPIOS_PER_REG) {
		return RESET_UNIT_GPIO_PAD_DATA_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 2) {
		return RESET_UNIT_GPIO2_PAD_DATA_REG_ADDR;
	} else if (id < GPIOS_PER_REG * 3) {
		return RESET_UNIT_GPIO3_PAD_DATA_REG_ADDR;
	} else {
		return RESET_UNIT_GPIO4_PAD_DATA_REG_ADDR;
	}
}

void GpioEnableOutput(uint32_t pin)
{
	uint32_t trien = ReadReg(GetTrienAddress(pin));

	trien &= ~(1 << (pin % GPIOS_PER_REG));
	WriteReg(GetTrienAddress(pin), trien);
}

void GpioDisableOutput(uint32_t pin)
{
	uint32_t trien = ReadReg(GetTrienAddress(pin));

	trien |= 1 << (pin % GPIOS_PER_REG);
	WriteReg(GetTrienAddress(pin), trien);
}

void GpioSet(uint32_t pin, uint32_t val)
{
	uint32_t pad_data = ReadReg(GetPadDataAddress(pin));

	pad_data &= ~(1 << (pin % GPIOS_PER_REG));
	pad_data |= (val & 0x1) << (pin % GPIOS_PER_REG);
	WriteReg(GetPadDataAddress(pin), pad_data);
}

void GpioRxEnable(uint32_t pin)
{
	uint32_t rxen = ReadReg(GetRxenAddress(pin));

	rxen |= 1 << (pin % GPIOS_PER_REG);
	WriteReg(GetRxenAddress(pin), rxen);
}

void GpioRxDisable(uint32_t pin)
{
	uint32_t rxen = ReadReg(GetRxenAddress(pin));

	rxen &= ~(1 << (pin % GPIOS_PER_REG));
	WriteReg(GetRxenAddress(pin), rxen);
}
