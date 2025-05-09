/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cm2dm_msg.h"
#include "fan_ctrl.h"
#include "fw_table.h"
#include "harvesting.h"
#include "pll.h"
#include "pvt.h"
#include "read_only_table.h"
#include "reg.h"
#include "regulator.h"
#include "status_reg.h"
#include "telemetry.h"
#include "telemetry_internal.h"
#include "gddr.h"

#include <float.h> /* for FLT_MAX */
#include <math.h>  /* for floor */
#include <stdint.h>
#include <string.h>

#include <tenstorrent/post_code.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(telemetry, CONFIG_TT_APP_LOG_LEVEL);

#define RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR 0x80030D20

struct telemetry_entry {
	uint16_t tag;
	uint16_t offset;
};

struct telemetry_table {
	uint32_t version;
	uint32_t entry_count;
	struct telemetry_entry tag_table[TELEM_ENUM_COUNT];
};

	/* Global variables */
static struct telemetry_table telemetry_table = {
	.version = TELEMETRY_VERSION,
	.entry_count = 0, /* set in init_telemetry */
	.tag_table = {
		{TAG_BOARD_ID_HIGH, BOARD_ID_HIGH},
		{TAG_BOARD_ID_LOW, BOARD_ID_LOW},
		{TAG_ASIC_ID, ASIC_ID},
		{TAG_HARVESTING_STATE, HARVESTING_STATE},
		{TAG_UPDATE_TELEM_SPEED, UPDATE_TELEM_SPEED},
		{TAG_VCORE, VCORE},
		{TAG_TDP, TDP},
		{TAG_TDC, TDC},
		{TAG_VDD_LIMITS, VDD_LIMITS},
		{TAG_THM_LIMITS, THM_LIMITS},
		{TAG_ASIC_TEMPERATURE, ASIC_TEMPERATURE},
		{TAG_VREG_TEMPERATURE, VREG_TEMPERATURE},
		{TAG_BOARD_TEMPERATURE, BOARD_TEMPERATURE},
		{TAG_AICLK, AICLK},
		{TAG_AXICLK, AXICLK},
		{TAG_ARCCLK, ARCCLK},
		{TAG_L2CPUCLK0, L2CPUCLK0},
		{TAG_L2CPUCLK1, L2CPUCLK1},
		{TAG_L2CPUCLK2, L2CPUCLK2},
		{TAG_L2CPUCLK3, L2CPUCLK3},
		{TAG_ETH_LIVE_STATUS, ETH_LIVE_STATUS},
		{TAG_GDDR_STATUS, GDDR_STATUS},
		{TAG_GDDR_SPEED, GDDR_SPEED},
		{TAG_ETH_FW_VERSION, ETH_FW_VERSION},
		{TAG_GDDR_FW_VERSION, GDDR_FW_VERSION},
		{TAG_DM_APP_FW_VERSION, DM_APP_FW_VERSION},
		{TAG_DM_BL_FW_VERSION, DM_BL_FW_VERSION},
		{TAG_FLASH_BUNDLE_VERSION, FLASH_BUNDLE_VERSION},
		{TAG_CM_FW_VERSION, CM_FW_VERSION},
		{TAG_L2CPU_FW_VERSION, L2CPU_FW_VERSION},
		{TAG_FAN_SPEED, FAN_SPEED},
		{TAG_TIMER_HEARTBEAT, TIMER_HEARTBEAT},
		{TAG_ENABLED_TENSIX_COL, ENABLED_TENSIX_COL},
		{TAG_ENABLED_ETH, ENABLED_ETH},
		{TAG_ENABLED_GDDR, ENABLED_GDDR},
		{TAG_ENABLED_L2CPU, ENABLED_L2CPU},
		{TAG_PCIE_USAGE, PCIE_USAGE},
		{TAG_INPUT_CURRENT, INPUT_CURRENT},
		{TAG_NOC_TRANSLATION, NOC_TRANSLATION},
		{TAG_FAN_RPM, FAN_RPM},
		{TAG_GDDR_0_1_TEMP, GDDR_0_1_TEMP},
		{TAG_GDDR_2_3_TEMP, GDDR_2_3_TEMP},
		{TAG_GDDR_4_5_TEMP, GDDR_4_5_TEMP},
		{TAG_GDDR_6_7_TEMP, GDDR_6_7_TEMP},
		{TAG_GDDR_0_1_CORR_ERRS, GDDR_0_1_CORR_ERRS},
		{TAG_GDDR_2_3_CORR_ERRS, GDDR_2_3_CORR_ERRS},
		{TAG_GDDR_4_5_CORR_ERRS, GDDR_4_5_CORR_ERRS},
		{TAG_GDDR_6_7_CORR_ERRS, GDDR_6_7_CORR_ERRS},
		{TAG_GDDR_UNCORR_ERRS, GDDR_UNCORR_ERRS},
		{TAG_MAX_GDDR_TEMP, MAX_GDDR_TEMP},
		{TAG_ASIC_LOCATION, ASIC_LOCATION},
		{TAG_BOARD_PWR_LIMIT, BOARD_PWR_LIMIT},
	}
};

static uint32_t telemetry[TELEM_ENUM_COUNT];

static struct k_timer telem_update_timer;
static struct k_work telem_update_worker;
static int telem_update_interval = 100;

uint32_t ConvertFloatToTelemetry(float value)
{
	/* Convert float to signed int 16.16 format */

	/* Handle error condition */
	if (value == FLT_MAX || value == -FLT_MAX) {
		return 0x80000000;
	}

	float abs_value = fabsf(value);
	uint16_t int_part = floor(abs_value);
	uint16_t frac_part = (abs_value - int_part) * 65536;
	uint32_t ret_value = (int_part << 16) | frac_part;
	/* Return the 2's complement if the original value was negative */
	if (value < 0) {
		ret_value = -ret_value;
	}
	return ret_value;
}

float ConvertTelemetryToFloat(int32_t value)
{
	/* Convert signed int 16.16 format to float */
	if (value == INT32_MIN) {
		return FLT_MAX;
	} else {
		return value / 65536.0;
	}
}

static void UpdateGddrTelemetry(void)
{
	uint32_t temperature[NUM_GDDR / 2] = {0,};
	uint32_t corr_errs[NUM_GDDR / 2] = {0,};

	uint32_t uncorr_errs = 0;
	uint32_t status = 0;

	for (int i = 0; i < NUM_GDDR; i++) {
		gddr_telemetry_table_t gddr_telemetry;
		/* Harvested instances should read 0b00 for status. */
		if (IS_BIT_SET(tile_enable.gddr_enabled, i)) {
			if (read_gddr_telemetry_table(i, &gddr_telemetry) < 0) {
				LOG_WRN_ONCE("Failed to read GDDR telemetry table while "
					     "updating telemetry");
				continue;
			}
			/* DDR Status:
			 * [0] - Training complete GDDR 0
			 * [1] - Error GDDR 0
			 * [2] - Training complete GDDR 1
			 * [3] - Error GDDR 1
			 * ...
			 * [14] - Training Complete GDDR 7
			 * [15] - Error GDDR 7
			 */
			status |= (gddr_telemetry.training_complete << (i * 2)) |
				(gddr_telemetry.gddr_error << (i * 2 + 1));

			/* DDR_x_y_TEMP:
			 * [31:24] GDDR y top
			 * [23:16] GDDR y bottom
			 * [15:8]  GDDR x top
			 * [7:0]   GDDR x bottom
			 */
			int shift_val = (i % 2) * 16;

			temperature[i / 2] |=
				((gddr_telemetry.dram_temperature_top & 0xff) << (8 + shift_val)) |
				((gddr_telemetry.dram_temperature_bottom & 0xff) << shift_val);

			/* GDDR_x_y_CORR_ERRS:
			 * [31:24] GDDR y Corrected Write EDC errors
			 * [23:16] GDDR y Corrected Read EDC Errors
			 * [15:8]  GDDR x Corrected Write EDC errors
			 * [7:0]   GDDR y Corrected Read EDC Errors
			 */
			corr_errs[i / 2] |=
				((gddr_telemetry.corr_edc_wr_errors & 0xff) << (8 + shift_val)) |
				((gddr_telemetry.corr_edc_rd_errors & 0xff) << shift_val);

			/* GDDR_UNCORR_ERRS:
			 * [0]  GDDR 0 Uncorrected Read EDC error
			 * [1]  GDDR 0 Uncorrected Write EDC error
			 * [2]  GDDR 1 Uncorrected Read EDC error
			 * ...
			 * [15] GDDR 7 Uncorrected Write EDC error
			 */
			uncorr_errs |=
				(gddr_telemetry.uncorr_edc_rd_error << (i * 2)) |
				(gddr_telemetry.uncorr_edc_wr_error << (i * 2 + 1));
			/* GDDR speed - in Mbps */
			telemetry[GDDR_SPEED] = gddr_telemetry.dram_speed;
		}
	}

	memcpy(telemetry + GDDR_0_1_TEMP, temperature, sizeof(temperature));
	memcpy(telemetry + GDDR_0_1_CORR_ERRS, corr_errs, sizeof(corr_errs));

	telemetry[GDDR_UNCORR_ERRS] = uncorr_errs;
	telemetry[GDDR_STATUS] = status;
}

int GetMaxGDDRTemp(void)
{
	int max_gddr_temp = 0;

	for (int i = 0; i < NUM_GDDR; i++) {
		int shift_val = (i % 2) * 16;

		max_gddr_temp =
			MAX(max_gddr_temp, (telemetry[GDDR_0_1_TEMP + i / 2] >> shift_val) & 0xFF);
		max_gddr_temp = MAX(max_gddr_temp,
				    (telemetry[GDDR_0_1_TEMP + i / 2] >> (shift_val + 8)) & 0xFF);
	}

	return max_gddr_temp;
}

static void write_static_telemetry(uint32_t app_version)
{
	/* Get the static values */
	telemetry[BOARD_ID_HIGH] = get_read_only_table()->board_id >> 32;
	telemetry[BOARD_ID_LOW] = get_read_only_table()->board_id & 0xFFFFFFFF;
	telemetry[ASIC_ID] = 0x00000000; /* Might be subject to redesign */
	telemetry[HARVESTING_STATE] = 0x00000000;
	telemetry[UPDATE_TELEM_SPEED] = telem_update_interval; /* Expected speed of update in ms */

	/* TODO: Gather FW versions from FW themselves */
	telemetry[ETH_FW_VERSION] = 0x00000000;
	if (tile_enable.gddr_enabled != 0) {
		gddr_telemetry_table_t gddr_telemetry;
		/* Use first available instance. */
		uint32_t gddr_inst = find_lsb_set(tile_enable.gddr_enabled) - 1;

		if (read_gddr_telemetry_table(gddr_inst, &gddr_telemetry) < 0) {
			LOG_WRN_ONCE("Failed to read GDDR telemetry table while "
				     "writing static telemetry");
		} else {
			telemetry[GDDR_FW_VERSION] = (gddr_telemetry.mrisc_fw_version_major << 16) |
						     gddr_telemetry.mrisc_fw_version_minor;
		}
	}
	/* DM_APP_FW_VERSION and DM_BL_FW_VERSION assumes zero-init, it might be
	 * initialized by bh_chip_set_static_info in dmfw already, must not clear.
	 */
	telemetry[FLASH_BUNDLE_VERSION] = get_fw_table()->fw_bundle_version;
	telemetry[CM_FW_VERSION] = app_version;
	telemetry[L2CPU_FW_VERSION] = 0x00000000;

	/* Tile enablement / harvesting information */
	telemetry[ENABLED_TENSIX_COL] = tile_enable.tensix_col_enabled;
	telemetry[ENABLED_ETH] = tile_enable.eth_enabled;
	telemetry[ENABLED_GDDR] = tile_enable.gddr_enabled;
	telemetry[ENABLED_L2CPU] = tile_enable.l2cpu_enabled;
	telemetry[PCIE_USAGE] =
		((tile_enable.pcie_usage[1] & 0x3) << 2) | (tile_enable.pcie_usage[0] & 0x3);
	/* telemetry[NOC_TRANSLATION] assumes zero-init, see also UpdateTelemetryNocTranslation. */

	if (get_pcb_type() == PcbTypeP300) {
		/* For the p300 a value of 1 is the left asic and 0 is the right */
		telemetry[ASIC_LOCATION] =
			FIELD_GET(BIT(6), ReadReg(RESET_UNIT_STRAP_REGISTERS_L_REG_ADDR));
	} else {
		/* For all other supported boards this value is 0 */
		telemetry[ASIC_LOCATION] = 0;
	}
}

static void update_telemetry(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_TELEMETRY_START);
	TelemetryInternalData telemetry_internal_data;

	ReadTelemetryInternal(telem_update_interval, &telemetry_internal_data);

	/* Get all dynamically updated values */
	telemetry[VCORE] =
		telemetry_internal_data
			.vcore_voltage; /* reported in mV, will be truncated to uint32_t */
	telemetry[TDP] = telemetry_internal_data
				 .vcore_power; /* reported in W, will be truncated to uint32_t */
	telemetry[TDC] = telemetry_internal_data
				 .vcore_current; /* reported in A, will be truncated to uint32_t */
	telemetry[VDD_LIMITS] = 0x00000000;      /* VDD limits - Not Available yet */
	telemetry[THM_LIMITS] = 0x00000000;      /* THM limits - Not Available yet */
	telemetry[ASIC_TEMPERATURE] = ConvertFloatToTelemetry(
		telemetry_internal_data.asic_temperature); /* ASIC temperature - reported in signed
							    * int 16.16 format
							    */
	telemetry[VREG_TEMPERATURE] = 0x000000;            /* VREG temperature - need I2C line */
	telemetry[BOARD_TEMPERATURE] = 0x000000;           /* Board temperature - need I2C line */
	telemetry[AICLK] = GetAICLK(); /* first 16 bits - MAX ASIC FREQ (Not Available yet), lower
					* 16 bits - current AICLK
					*/
	telemetry[AXICLK] = GetAXICLK();
	telemetry[ARCCLK] = GetARCCLK();
	telemetry[L2CPUCLK0] = GetL2CPUCLK(0);
	telemetry[L2CPUCLK1] = GetL2CPUCLK(1);
	telemetry[L2CPUCLK2] = GetL2CPUCLK(2);
	telemetry[L2CPUCLK3] = GetL2CPUCLK(3);
	telemetry[ETH_LIVE_STATUS] =
		0x00000000; /* ETH live status lower 16 bits: heartbeat status, upper 16 bits:
			     * retrain_status - Not Available yet
			     */
	telemetry[FAN_SPEED] = GetFanSpeed(); /* Target fan speed - reported in percentage */
	telemetry[FAN_RPM] = GetFanRPM();     /* Actual fan RPM */
	UpdateGddrTelemetry();
	telemetry[MAX_GDDR_TEMP] = GetMaxGDDRTemp();
	telemetry[INPUT_CURRENT] =
		GetInputCurrent();    /* Input current - reported in A in signed int 16.16 format */
	telemetry[TIMER_HEARTBEAT]++; /* Incremented every time the timer is called */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_TELEMETRY_END);
}

/* Handler functions for zephyr timer and worker objects */
static void telemetry_work_handler(struct k_work *work)
{
	/* Repeat fetching of dynamic telemetry values */
	update_telemetry();
}
static void telemetry_timer_handler(struct k_timer *timer)
{
	k_work_submit(&telem_update_worker);
}

/* Zephyr timer object submits a work item to the system work queue whose thread performs the task
 * on a periodic basis.
 */
/* See:
 * https://docs.zephyrproject.org/latest/kernel/services/timing/timers.html#using-a-timer-expiry-function
 */
static K_WORK_DEFINE(telem_update_worker, telemetry_work_handler);
static K_TIMER_DEFINE(telem_update_timer, telemetry_timer_handler, NULL);

static uint32_t count_valid_entries(void)
{
	for (uint32_t i = ARRAY_SIZE(telemetry_table.tag_table); i > 0; i--) {
		if (telemetry_table.tag_table[i-1].tag != 0) {
			return i;
		}
	}

	return 0;
}

void init_telemetry(uint32_t app_version)
{
	telemetry_table.entry_count = count_valid_entries();
	write_static_telemetry(app_version);
	/* fill the dynamic values once before starting timed updates */
	update_telemetry();

	/* Publish the telemetry data pointer for readers in Scratch RAM */
	WriteReg(TELEMETRY_DATA_REG_ADDR, POINTER_TO_UINT(&telemetry[0]));
	WriteReg(TELEMETRY_TABLE_REG_ADDR, POINTER_TO_UINT(&telemetry_table));
}

void StartTelemetryTimer(void)
{
	/* Start the timer to update the dynamic telemetry values
	 * Duration (time interval before the timer expires for the first time) and
	 * Period (time interval between all timer expirations after the first one)
	 * are both set to telem_update_interval
	 */
	k_timer_start(&telem_update_timer, K_MSEC(telem_update_interval),
		      K_MSEC(telem_update_interval));
}

void UpdateDmFwVersion(uint32_t bl_version, uint32_t app_version)
{
	telemetry[DM_BL_FW_VERSION] = bl_version;
	telemetry[DM_APP_FW_VERSION] = app_version;
}

void UpdateTelemetryNocTranslation(bool translation_enabled)
{
	/* Note that this may be called before init_telemetry. */
	telemetry[NOC_TRANSLATION] = translation_enabled;
}

void UpdateTelemetryBoardPwrLimit(uint32_t pwr_limit)
{
	telemetry[BOARD_PWR_LIMIT] = pwr_limit;
}
