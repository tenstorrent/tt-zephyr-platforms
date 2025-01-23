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
#include "smbus_target.h"
#include "fw_table.h"

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

int main(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ZEPHYR_INIT_DONE);
	printk("Tenstorrent Blackhole CMFW %s\n", APP_VERSION_STRING);

	InitFW();
	InitHW();

	init_msgqueue();

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
