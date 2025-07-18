/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "reg.h"
#include "timer.h"
#include "pvt.h"

#include <stdbool.h>

#include <tenstorrent/post_code.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#define RESET_UNIT_CATMON_THERM_TRIP_STATUS_REG_ADDR  0x80030164
#define RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR    0x80030168
#define RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_DEFAULT 0x00000318

#define CAT_EARLY_TRIP_TEMP 100
#define T_J_SHUTDOWN        110 /* BH Prod Spec 7.3 */
/* It would be more principled to use the nearly-worst-case 25C error
 * from the datasheet, but previously catmon was set to 100C.
 */
#define DEFAULT_CALIBRATION (CAT_EARLY_TRIP_TEMP - T_J_SHUTDOWN)

#define TRIM_CODE_BITS 6

typedef struct {
	uint32_t trim_code: TRIM_CODE_BITS;
	uint32_t rsvd_0: 1;
	uint32_t enable: 1;
	uint32_t pll_therm_trip_bypass_catmon_en: 1;
	uint32_t pll_therm_trip_bypass_thermb_en: 1;
} RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_t f;
} RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_u;

/* catmon trim codes run from 0: 196C+ to 63: -56C+, evenly spaced 4C */

static uint8_t TempToTrimCode(float temp)
{
	temp = CLAMP(temp, -56, 196);
	return 49 - temp / 4;
}

#ifndef CONFIG_TT_SMC_RECOVERY
static float TrimCodeToTemp(int32_t trim_code)
{
	/* 198: 196+2 to return a value in the
	 * middle of the 4C trim code interval.
	 */
	return 198 - 4 * trim_code;
}
#endif

/* Datasheet gives 5us for outputs to be settle after enabling.
 * We assume this is enough for any trim code change.
 */
static void WaitCATUpdate(void)
{
	WaitUs(5);
}

static const struct device *gpio1 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(gpio1));

static void EnableCAT(uint8_t trim_code, bool shutdown_on_trip)
{
	/* CAT output is not stable during initialization,
	 * disable therm trip GPIO and PLL bypass to avoid false therm trip indication
	 */

	gpio_pin_configure(gpio1, 15, GPIO_DISCONNECTED);

	RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_u cat_cntl;

	cat_cntl.val = RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_DEFAULT;
	cat_cntl.f.trim_code = trim_code;
	cat_cntl.f.enable = 1;
	cat_cntl.f.pll_therm_trip_bypass_catmon_en = 0;
	cat_cntl.f.pll_therm_trip_bypass_thermb_en = 0;
	WriteReg(RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR, cat_cntl.val);

	WaitCATUpdate();

	if (shutdown_on_trip) {
		/* CAT initialization complete, enable therm trip GPIO and PLL bypass */
		gpio_pin_configure(gpio1, 15, GPIO_OUTPUT);
		cat_cntl.f.pll_therm_trip_bypass_catmon_en = 1;
		cat_cntl.f.pll_therm_trip_bypass_thermb_en = 1;
		WriteReg(RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR, cat_cntl.val);
	}
}

static int CATEarlyInit(void)
{
	if (!IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	EnableCAT(TempToTrimCode(CAT_EARLY_TRIP_TEMP), true);
	return 0;
}
SYS_INIT(CATEarlyInit, APPLICATION, 4);

#ifndef CONFIG_TT_SMC_RECOVERY
/* Calibrate catmon against thermal sensors by looping over the
 * catmon trim codes until it stops triggering. This is linear
 * search. Binary may be faster but must consider that the target
 * is moving.
 */
static float CalibrateCAT(void)
{
	EnableCAT(0, false);

	/* Not possible that it's already 196C. */
	if (ReadReg(RESET_UNIT_CATMON_THERM_TRIP_STATUS_REG_ADDR)) {
		return DEFAULT_CALIBRATION;
	}

	RESET_UNIT_CATMON_THERM_TRIP_CNTL_reg_u cat_cntl;

	cat_cntl.val = RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_DEFAULT;
	cat_cntl.f.enable = 1;
	cat_cntl.f.pll_therm_trip_bypass_catmon_en = 0;
	cat_cntl.f.pll_therm_trip_bypass_thermb_en = 0;

	unsigned int code;
	bool tripped = false;

	for (code = 0; code <= BIT_MASK(TRIM_CODE_BITS) && !tripped; code++) {
		cat_cntl.f.trim_code = code;
		WriteReg(RESET_UNIT_CATMON_THERM_TRIP_CNTL_REG_ADDR, cat_cntl.val);

		WaitCATUpdate();

		tripped = ReadReg(RESET_UNIT_CATMON_THERM_TRIP_STATUS_REG_ADDR);
	}

	if (!tripped) {
		return DEFAULT_CALIBRATION;
	}

	float catmon_temp = TrimCodeToTemp(code);
	float ts_temp = GetAvgChipTemp();
	float catmon_error = catmon_temp - ts_temp;

	return catmon_error;
}

static int CATInit(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPF);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	float catmon_error = CalibrateCAT();

	EnableCAT(TempToTrimCode(T_J_SHUTDOWN + catmon_error), true);
	return 0;
}
SYS_INIT(CATInit, APPLICATION, 20);
#endif
