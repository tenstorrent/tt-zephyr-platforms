/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include "noc_init.h"

#include <zephyr/logging/log.h>
#include "aiclk_ppm.h"
#include "gddr.h"

LOG_MODULE_REGISTER(power, CONFIG_TT_APP_LOG_LEVEL);

enum power_bit_flags_e {
	power_bit_flag_aiclk,
	power_bit_flag_mrisc,
	power_bit_flag_tensix,
	power_bit_flag_max
};

enum power_settings_e {
	power_settings_max
};

static int32_t apply_power_settings(const struct power_setting_rqst *power_setting)
{
	int32_t ret = 0;

	if (power_setting->power_flags_valid > power_bit_flag_aiclk) {
		aiclk_set_busy(power_setting->power_flags_bitfield.max_ai_clk);
	}

	if (power_setting->power_flags_valid > power_bit_flag_mrisc) {
		ret = set_mrisc_power_setting(power_setting->power_flags_bitfield.mrisc_phy_power);
	}

	if (power_setting->power_flags_valid > power_bit_flag_tensix) {
		ret = set_tensix_enable(power_setting->power_flags_bitfield.tensix_enable);
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

	if (power_setting->power_flags_valid > power_bit_flag_max) {
		LOG_WRN("Host request to apply %u power flags. SMC FW supports only %u",
			power_setting->power_flags_valid, power_bit_flag_max);
	}

	if (power_setting->power_settings_valid > power_settings_max) {
		LOG_WRN("Host request to apply %u power settings. SMC FW supports only %u",
			power_setting->power_settings_valid, power_settings_max);
	}
	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_POWER_SETTING, power_setting_msg_handler);
