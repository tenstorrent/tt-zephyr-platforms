/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include "vf_curve.h"
#include "fw_table.h"

void InitVFCurve(void)
{
	/* Do nothing for now */
}

/**
 * @brief Calculate the voltage based on the frequency
 *
 * @param freq_mhz The frequency in MHz
 * @return The voltage in mV
 */
float VFCurve(float freq_mhz)
{
	float freq_margin_mhz = get_fw_table()->chip_limits.frequency_margin;
	float freq_with_margin_mhz = freq_mhz + freq_margin_mhz;

	float voltage_margin_mv = get_fw_table()->chip_limits.voltage_margin;
	float voltage_mv = 0.00031395F * freq_with_margin_mhz * freq_with_margin_mhz -
			   0.43953F * freq_with_margin_mhz + 828.83F;

	return voltage_mv + voltage_margin_mv;
}
