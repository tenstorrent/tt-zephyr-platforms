/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cm2dm_msg.h"
#include "dvfs.h"
#include "fan_ctrl.h"
#include "fw_table.h"
#include "init_common.h"
#include "smbus_target.h"
#include "telemetry.h"
#include "status_reg.h"
#include "reg.h"

#include <stdint.h>

#include <app_version.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/uart_tt_virt.h>
#include <tenstorrent/post_code.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/watchdog.h>

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

static const struct device *const wdt0 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(wdt0));

int main(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ZEPHYR_INIT_DONE);
	printk("Tenstorrent Blackhole CMFW %s\n", APP_VERSION_STRING);

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		if (get_fw_table()->feature_enable.aiclk_ppm_en) {
			STATUS_ERROR_STATUS0_reg_u error_status0 = {
				.val = ReadReg(STATUS_ERROR_STATUS0_REG_ADDR)
			};

			if (error_status0.f.regulator_init_error) {
				LOG_ERR("Not enabling AICLK PPM due to regulator init error.\n");
			} else {
				/* DVFS should get enabled if AICLK PPM or L2CPUCLK PPM is enabled
				 * We currently don't have plans to implement L2CPUCLK PPM,
				 * so currently, dvfs_enable == aiclk_ppm_enable
				 */
				InitDVFS();
			}
		}
	}

	init_msgqueue();

	if (!IS_ENABLED(CONFIG_TT_SMC_RECOVERY)) {
		init_telemetry(APPVERSION);
		if (get_fw_table()->feature_enable.fan_ctrl_en) {
			init_fan_ctrl();
		}

		/* These timers are split out from their init functions since their work tasks have
		 * i2c conflicts with other init functions.
		 *
		 * Note: The above issue would be solved by using Zephyr's driver model.
		 */
		StartTelemetryTimer();
		if (dvfs_enabled) {
			StartDVFSTimer();
		}

		if (!DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(wdt0)) ||
		    (get_fw_table()->chip_limits.auto_reset_timeout == 0)) {
			/* How to solve this? Not going through the device API */
			UpdateAutoResetTimeoutRequest(0);
		}
	}

	Dm2CmReadyRequest();

	while (1) {
		k_msleep(250);
		wdt_feed(wdt0, 0);
	}

	return 0;
}

#define FW_VERSION_SEMANTIC APPVERSION
#define FW_VERSION_DATE     0x00000000
#define FW_VERSION_LOW      0x00000000
#define FW_VERSION_HIGH     0x00000000

uint32_t FW_VERSION[4] __attribute__((section(".fw_version"))) = {
	FW_VERSION_SEMANTIC, FW_VERSION_DATE, FW_VERSION_LOW, FW_VERSION_HIGH};

static int _InitFW(void)
{
	return InitFW(APPVERSION);
}

SYS_INIT(_InitFW, APPLICATION, UTIL_DEC(CONFIG_TT_BH_ARC_SYSINIT_PRIORITY));

#ifdef CONFIG_UART_TT_VIRT
#include "status_reg.h"

void uart_tt_virt_init_callback(const struct device *dev, size_t inst)
{
	sys_write32((uint32_t)(uintptr_t)uart_tt_virt_get(dev), STATUS_FW_VUART_REG_ADDR(inst));
}
#endif
