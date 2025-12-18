/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/bh_power.h>
#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>

#include <zephyr/logging/log.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <zephyr/kernel.h>

#include "noc_init.h"
#include "aiclk_ppm.h"
#include "gddr.h"
#include "bh_reset.h"

LOG_MODULE_REGISTER(power, CONFIG_TT_APP_LOG_LEVEL);
static const struct device *pll4 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pll4));
static const struct device *fwtable_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(fwtable));

enum power_bit_flags_e {
	power_bit_flag_aiclk,
	power_bit_flag_mrisc,
	power_bit_flag_tensix,
	power_bit_flag_l2cpu,
	power_bit_flag_max
};

static bool power_state[power_bit_flag_max] = {
	false,
	true,
	true,
	true,
};

enum power_settings_e {
	power_settings_max
};

int32_t bh_set_l2cpu_enable(bool enable)
{
	int32_t ret = 0;

	if (enable) {
		ret = clock_control_on(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_0);
		ret = clock_control_on(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_1);
		ret = clock_control_on(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_2);
		ret = clock_control_on(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_3);
	} else {
		ret = clock_control_off(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_0);
		ret = clock_control_off(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_1);
		ret = clock_control_off(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_2);
		ret = clock_control_off(
			pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_3);
	}

	return ret;
}

bool bh_get_aiclk_busy(void)
{
	return power_state[power_bit_flag_aiclk];
}

static int32_t apply_power_settings(const struct power_setting_rqst *power_setting)
{
	int32_t ret = 0;

	if (power_setting->power_flags_valid > power_bit_flag_aiclk) {
		power_state[power_bit_flag_aiclk] = power_setting->power_flags_bitfield.max_ai_clk;
		aiclk_update_busy();
	}

	if (power_setting->power_flags_valid > power_bit_flag_tensix) {
		bool reset_hit = false;

		/*We track whether or not the tensix is already clock gated because
		 *we can't send the reset message with the clocks gated. So, only reset the tensix
		 *cores if the tensix is not clock gated.
		 */
		if (!power_setting->power_flags_bitfield.tensix_enable &&
		    power_state[power_bit_flag_tensix]) {
			bh_soft_reset_all_tensix();
			k_usleep(100);
			reset_hit = true;
		}

		ret = set_tensix_enable(power_setting->power_flags_bitfield.tensix_enable);
		power_state[power_bit_flag_tensix] =
			power_setting->power_flags_bitfield.tensix_enable;

		/*Note, if we're turning on the tensixes, we don't take them out of reset,
		 *we just lift the clock gating.
		 */
	}
	if (power_setting->power_flags_valid > power_bit_flag_l2cpu) {
		ret = bh_set_l2cpu_enable(power_setting->power_flags_bitfield.l2cpu_enable);
		power_state[power_bit_flag_l2cpu] =
			power_setting->power_flags_bitfield.l2cpu_enable;
	}

	if (power_setting->power_flags_valid > power_bit_flag_mrisc) {
		/* Get board type from firmware table */
		uint8_t board_type = tt_bh_fwtable_get_board_type(fwtable_dev);

		/* Skip mrisc power setting for GALAXY boards (BOARDTYPE_UBB) */
		if (board_type != BOARDTYPE_UBB) {
			ret = set_mrisc_power_setting(
				power_setting->power_flags_bitfield.mrisc_phy_power);
			power_state[power_bit_flag_mrisc] =
				power_setting->power_flags_bitfield.mrisc_phy_power;
		}
	}

	return ret;
}

/** @brief Handles the request to set the power settings
 * @param[in] request The request, of type @ref power_setting_rqst_t, with command code
 *	@ref TT_SMC_MSG_POWER_SETTING
 * @param[out] response The response to the host
 * @return 0 for success. 1 for Failure.
 */
static uint8_t power_setting_msg_handler(const union request *request, struct response *response)
{
	const struct power_setting_rqst *power_setting = &request->power_setting;

	apply_power_settings(power_setting);
	LOG_INF("Power State: GDDR-%u Tensix-%u AICLK-%u, L2CPU-%u",
		power_state[power_bit_flag_mrisc], power_state[power_bit_flag_tensix],
		power_state[power_bit_flag_aiclk], power_state[power_bit_flag_l2cpu]);

	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_POWER_SETTING, power_setting_msg_handler);
