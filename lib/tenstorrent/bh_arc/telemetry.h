/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define TELEMETRY_VERSION 0x00000100 /* v0.1.0 - Only update when redefining the
				      * meaning of an existing tag
				      * Semver format: 0 x 00 Major Minor Patch
				      */

/* Tags - these will be guaranteed and will not change */
#define TAG_BOARD_ID_HIGH        1
#define TAG_BOARD_ID_LOW         2
#define TAG_ASIC_ID              3
#define TAG_HARVESTING_STATE     4
#define TAG_UPDATE_TELEM_SPEED   5
#define TAG_VCORE                6
#define TAG_TDP                  7
#define TAG_TDC                  8
#define TAG_VDD_LIMITS           9
#define TAG_THM_LIMITS           10
#define TAG_ASIC_TEMPERATURE     11
#define TAG_VREG_TEMPERATURE     12
#define TAG_BOARD_TEMPERATURE    13
#define TAG_AICLK                14
#define TAG_AXICLK               15
#define TAG_ARCCLK               16
#define TAG_L2CPUCLK0            17
#define TAG_L2CPUCLK1            18
#define TAG_L2CPUCLK2            19
#define TAG_L2CPUCLK3            20
#define TAG_ETH_LIVE_STATUS      21
#define TAG_GDDR_STATUS          22
#define TAG_GDDR_SPEED           23
#define TAG_ETH_FW_VERSION       24
#define TAG_GDDR_FW_VERSION      25
#define TAG_DM_APP_FW_VERSION    26
#define TAG_DM_BL_FW_VERSION     27
#define TAG_FLASH_BUNDLE_VERSION 28
#define TAG_CM_FW_VERSION        29
#define TAG_L2CPU_FW_VERSION     30
#define TAG_FAN_SPEED            31
#define TAG_TIMER_HEARTBEAT      32
#define TAG_TELEM_ENUM_COUNT     33
#define TAG_ENABLED_TENSIX_COL   34
#define TAG_ENABLED_ETH          35
#define TAG_ENABLED_GDDR         36
#define TAG_ENABLED_L2CPU        37
#define TAG_PCIE_USAGE           38
#define TAG_INPUT_CURRENT        39
#define TAG_NOC_TRANSLATION      40
#define TAG_FAN_RPM              41
#define TAG_GDDR_0_1_TEMP        42
#define TAG_GDDR_2_3_TEMP        43
#define TAG_GDDR_4_5_TEMP        44
#define TAG_GDDR_6_7_TEMP        45
#define TAG_GDDR_0_1_CORR_ERRS   46
#define TAG_GDDR_2_3_CORR_ERRS   47
#define TAG_GDDR_4_5_CORR_ERRS   48
#define TAG_GDDR_6_7_CORR_ERRS   49
#define TAG_GDDR_UNCORR_ERRS     50
#define TAG_MAX_GDDR_TEMP        51
#define TAG_ASIC_LOCATION        52
#define TAG_BOARD_PWR_LIMIT      53

/* Enums are subject to updates */
typedef enum {
	/* Board static information */
	BOARD_ID_HIGH,
	BOARD_ID_LOW,
	ASIC_ID,
	HARVESTING_STATE,

	/* Telemetry timing data */
	UPDATE_TELEM_SPEED, /* Expected speed of update to telemetry in ms */

	/* Regulator information */
	VCORE,
	TDP,
	TDC,
	VDD_LIMITS,
	THM_LIMITS,

	/* Temperature information */
	ASIC_TEMPERATURE,
	VREG_TEMPERATURE,
	BOARD_TEMPERATURE,

	/* Clock information */
	AICLK,
	AXICLK,
	ARCCLK,
	L2CPUCLK0,
	L2CPUCLK1,
	L2CPUCLK2,
	L2CPUCLK3,

	/* IO information */
	ETH_LIVE_STATUS, /* Lower 16 bits - heartbeat status, upper 16 bits - retrain_status */
	GDDR_STATUS,
	GDDR_SPEED,

	/* FW versions */
	ETH_FW_VERSION,
	GDDR_FW_VERSION,
	/* Board manager fw versions */
	DM_APP_FW_VERSION,
	DM_BL_FW_VERSION,
	FLASH_BUNDLE_VERSION,
	CM_FW_VERSION,
	L2CPU_FW_VERSION,

	/* MISC */
	FAN_SPEED,
	FAN_RPM,
	TIMER_HEARTBEAT, /* Incremented every time the timer is called */
	INPUT_CURRENT,
	BOARD_PWR_LIMIT,

	/* Tile enablement/harvesting information */
	ENABLED_TENSIX_COL,
	ENABLED_ETH,
	ENABLED_GDDR,
	ENABLED_L2CPU,
	PCIE_USAGE,
	NOC_TRANSLATION,

	/* DRAM Temperatures */
	GDDR_0_1_TEMP,
	GDDR_2_3_TEMP,
	GDDR_4_5_TEMP,
	GDDR_6_7_TEMP,
	MAX_GDDR_TEMP,

	/* DDR Errors */
	GDDR_0_1_CORR_ERRS,
	GDDR_2_3_CORR_ERRS,
	GDDR_4_5_CORR_ERRS,
	GDDR_6_7_CORR_ERRS,
	GDDR_UNCORR_ERRS,

	ASIC_LOCATION,

	TELEM_ENUM_COUNT, /* Count to check how large the enum is */
} Telemetry;

void init_telemetry(uint32_t app_version);
uint32_t ConvertFloatToTelemetry(float value);
float ConvertTelemetryToFloat(int32_t value);
int GetMaxGDDRTemp(void);
void StartTelemetryTimer(void);
void UpdateDmFwVersion(uint32_t bl_version, uint32_t app_version);
void UpdateTelemetryNocTranslation(bool translation_enabled);
void UpdateTelemetryBoardPwrLimit(uint32_t pwr_limit);

#endif
