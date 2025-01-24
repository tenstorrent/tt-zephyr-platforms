/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <app_version.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/post_code.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "init.h"
#include "init_common.h"
#include "reg.h"
#include "status_reg.h"
#include "telemetry_internal.h"
#include "telemetry.h"
#include "smbus_target.h"
#include "dvfs.h"
#include "fw_table.h"

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

int main(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ZEPHYR_INIT_DONE);
	printk("Tenstorrent Blackhole CMFW %s\n", APP_VERSION_STRING);

	InitFW();
	InitHW();

	if (get_fw_table()->feature_enable.aiclk_ppm_en) {
		/* DVFS should get enabled if AICLK PPM or L2CPUCLK PPM is enabled */
		/* We currently don't have plans to implement L2CPUCLK PPM, */
		/* so currently, dvfs_enable == aiclk_ppm_enable */
		InitDVFS();
	}

	init_msgqueue();
	init_telemetry();

	/* These timers are split out from their init functions since their work tasks have i2c
	 * conflicts with other init functions
	 */
	/* Consider moving init before main (using Zephyr SYS_INIT()) to be pre ISR enablement */
	StartTelemetryTimer();
	if (dvfs_enabled) {
		StartDVFSTimer();
	}

	while (1) {
		PollSmbusTarget();
		k_yield();
	}

	return 0;
}

#define FW_VERSION_SEMANTIC APPVERSION
#define FW_VERSION_DATE     0x00000000
#define FW_VERSION_LOW      0x00000000
#define FW_VERSION_HIGH     0x00000000

uint32_t FW_VERSION[4] __attribute__((section(".fw_version"))) = {
	FW_VERSION_SEMANTIC, FW_VERSION_DATE, FW_VERSION_LOW, FW_VERSION_HIGH};
