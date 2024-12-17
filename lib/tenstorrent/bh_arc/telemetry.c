/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cm2bm_msg.h"
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

#include <float.h> /* for FLT_MAX */
#include <math.h>  /* for floor */
#include <stdint.h>
#include <string.h>

#include <tenstorrent/post_code.h>
#include <zephyr/kernel.h>

struct telemetry_entry {
	uint16_t tag;
	uint16_t offset;
};

struct telemetry_table {
	uint32_t version;
	uint32_t entry_count;
	struct telemetry_entry tag_table[TELEM_ENUM_COUNT];
	uint32_t telemetry[TELEM_ENUM_COUNT];
};

/* Global variables */
static struct telemetry_table telemetry_table;
static uint32_t *telemetry = &telemetry_table.telemetry[0];
static struct telemetry_entry *tag_table = &telemetry_table.tag_table[0];

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

static void write_static_telemetry(uint32_t app_version)
{
	telemetry_table.version =
		TELEMETRY_VERSION; /* v0.1.0 - Please update when changes are made */
	telemetry_table.entry_count = TELEM_ENUM_COUNT; /* Runtime count of telemetry entries */

	/* Get the static values */
	telemetry[BOARD_ID_HIGH] = get_read_only_table()->board_id >> 32;
	telemetry[BOARD_ID_LOW] = get_read_only_table()->board_id & 0xFFFFFFFF;
	telemetry[ASIC_ID] = 0x00000000; /* Might be subject to redesign */
	telemetry[HARVESTING_STATE] = 0x00000000;
	telemetry[UPDATE_TELEM_SPEED] = telem_update_interval; /* Expected speed of update in ms */

	/* TODO: Gather FW versions from FW themselves */
	telemetry[ETH_FW_VERSION] = 0x00000000;
	telemetry[DDR_FW_VERSION] = 0x00000000;
	telemetry[BM_APP_FW_VERSION] = 0x00000000;
	telemetry[BM_BL_FW_VERSION] = 0x00000000;
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
	telemetry[DDR_STATUS] = 0x00000000;   /* DDR status - Not Available yet */
	telemetry[DDR_SPEED] = 0x00000000;    /* DDR speed - Not Available yet */
	telemetry[FAN_SPEED] = GetFanSpeed(); /* Target fan speed - reported in percentage */
	telemetry[FAN_RPM] = GetFanRPM();     /* Actual fan RPM */
	telemetry[INPUT_CURRENT] =
		GetInputCurrent();    /* Input current - reported in A in signed int 16.16 format */
	telemetry[TIMER_HEARTBEAT]++; /* Incremented every time the timer is called */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_TELEMETRY_END);
}

static void update_tag_table(void)
{
	tag_table[0] = (struct telemetry_entry){TAG_BOARD_ID_HIGH, BOARD_ID_HIGH};
	tag_table[1] = (struct telemetry_entry){TAG_BOARD_ID_LOW, BOARD_ID_LOW};
	tag_table[2] = (struct telemetry_entry){TAG_ASIC_ID, ASIC_ID};
	tag_table[3] = (struct telemetry_entry){TAG_HARVESTING_STATE, HARVESTING_STATE};
	tag_table[4] = (struct telemetry_entry){TAG_UPDATE_TELEM_SPEED, UPDATE_TELEM_SPEED};
	tag_table[5] = (struct telemetry_entry){TAG_VCORE, VCORE};
	tag_table[6] = (struct telemetry_entry){TAG_TDP, TDP};
	tag_table[7] = (struct telemetry_entry){TAG_TDC, TDC};
	tag_table[8] = (struct telemetry_entry){TAG_VDD_LIMITS, VDD_LIMITS};
	tag_table[9] = (struct telemetry_entry){TAG_THM_LIMITS, THM_LIMITS};
	tag_table[10] = (struct telemetry_entry){TAG_ASIC_TEMPERATURE, ASIC_TEMPERATURE};
	tag_table[11] = (struct telemetry_entry){TAG_VREG_TEMPERATURE, VREG_TEMPERATURE};
	tag_table[12] = (struct telemetry_entry){TAG_BOARD_TEMPERATURE, BOARD_TEMPERATURE};
	tag_table[13] = (struct telemetry_entry){TAG_AICLK, AICLK};
	tag_table[14] = (struct telemetry_entry){TAG_AXICLK, AXICLK};
	tag_table[15] = (struct telemetry_entry){TAG_ARCCLK, ARCCLK};
	tag_table[16] = (struct telemetry_entry){TAG_L2CPUCLK0, L2CPUCLK0};
	tag_table[17] = (struct telemetry_entry){TAG_L2CPUCLK1, L2CPUCLK1};
	tag_table[18] = (struct telemetry_entry){TAG_L2CPUCLK2, L2CPUCLK2};
	tag_table[19] = (struct telemetry_entry){TAG_L2CPUCLK3, L2CPUCLK3};
	tag_table[20] = (struct telemetry_entry){TAG_ETH_LIVE_STATUS, ETH_LIVE_STATUS};
	tag_table[21] = (struct telemetry_entry){TAG_DDR_STATUS, DDR_STATUS};
	tag_table[22] = (struct telemetry_entry){TAG_DDR_SPEED, DDR_SPEED};
	tag_table[23] = (struct telemetry_entry){TAG_ETH_FW_VERSION, ETH_FW_VERSION};
	tag_table[24] = (struct telemetry_entry){TAG_DDR_FW_VERSION, DDR_FW_VERSION};
	tag_table[25] = (struct telemetry_entry){TAG_BM_APP_FW_VERSION, BM_APP_FW_VERSION};
	tag_table[26] = (struct telemetry_entry){TAG_BM_BL_FW_VERSION, BM_BL_FW_VERSION};
	tag_table[27] = (struct telemetry_entry){TAG_FLASH_BUNDLE_VERSION, FLASH_BUNDLE_VERSION};
	tag_table[28] = (struct telemetry_entry){TAG_CM_FW_VERSION, CM_FW_VERSION};
	tag_table[29] = (struct telemetry_entry){TAG_L2CPU_FW_VERSION, L2CPU_FW_VERSION};
	tag_table[30] = (struct telemetry_entry){TAG_FAN_SPEED, FAN_SPEED};
	tag_table[31] = (struct telemetry_entry){TAG_TIMER_HEARTBEAT, TIMER_HEARTBEAT};
	tag_table[32] = (struct telemetry_entry){TAG_ENABLED_TENSIX_COL, ENABLED_TENSIX_COL};
	tag_table[33] = (struct telemetry_entry){TAG_ENABLED_ETH, ENABLED_ETH};
	tag_table[34] = (struct telemetry_entry){TAG_ENABLED_GDDR, ENABLED_GDDR};
	tag_table[35] = (struct telemetry_entry){TAG_ENABLED_L2CPU, ENABLED_L2CPU};
	tag_table[36] = (struct telemetry_entry){TAG_PCIE_USAGE, PCIE_USAGE};
	tag_table[37] = (struct telemetry_entry){TAG_INPUT_CURRENT, INPUT_CURRENT};
	tag_table[38] = (struct telemetry_entry){TAG_NOC_TRANSLATION, NOC_TRANSLATION};
	tag_table[39] = (struct telemetry_entry){TAG_FAN_RPM, FAN_RPM};
	tag_table[40] = (struct telemetry_entry){TAG_TELEM_ENUM_COUNT, TELEM_ENUM_COUNT};
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

void init_telemetry(uint32_t app_version)
{
	update_tag_table();
	write_static_telemetry(app_version);
	/* fill the dynamic values once before starting timed updates */
	update_telemetry();

	/* Publish the telemetry data pointer for readers in Scratch RAM */
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(12), (uint32_t)&telemetry[0]);
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(13), (uint32_t)&telemetry_table);
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

void UpdateBmFwVersion(uint32_t bl_version, uint32_t app_version)
{
	telemetry[BM_BL_FW_VERSION] = bl_version;
	telemetry[BM_APP_FW_VERSION] = app_version;
}

void UpdateTelemetryNocTranslation(bool translation_enabled)
{
	/* Note that this may be called before init_telemetry. */
	telemetry[NOC_TRANSLATION] = translation_enabled;
}
