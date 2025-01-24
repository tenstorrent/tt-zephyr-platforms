/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gpio.h"
#include "reg.h"
#include "timer.h"

#define RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR    0x80030168
#define RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_DEFAULT 0x00000318
#define CAT_THERM_TRIP_TEMP                           100

typedef struct {
	uint32_t trim_code: 6;
	uint32_t rsvd_0: 1;
	uint32_t enable: 1;
	uint32_t pll_therm_trip_bypass_catmon_en: 1;
	uint32_t pll_therm_trip_bypass_thermb_en: 1;
} RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_t f;
} RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_u;

static uint8_t TempToTrimCode(float temp)
{
	return (uint8_t)((194 - temp) / 4);
}

void CATInit(void)
{
	GpioDisableOutput(GPIO_THERM_TRIP);
	RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_u cat_cntl;

	cat_cntl.val = RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_DEFAULT;
	cat_cntl.f.trim_code = TempToTrimCode(CAT_THERM_TRIP_TEMP);
	cat_cntl.f.enable = 1;
	cat_cntl.f.pll_therm_trip_bypass_catmon_en = 0;
	cat_cntl.f.pll_therm_trip_bypass_thermb_en = 0;
	WriteReg(RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR, cat_cntl.val);
	/* CAT output is undefined during boot, disable PLL bypass and therm_trip GPIO */
	Wait(5 * WAIT_1US);
	GpioEnableOutput(GPIO_THERM_TRIP);
	cat_cntl.f.pll_therm_trip_bypass_catmon_en = 1;
	cat_cntl.f.pll_therm_trip_bypass_thermb_en = 1;
	WriteReg(RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR, cat_cntl.val);
}
