/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include "vf_curve.h"
#include "fw_table.h"

/* Bounds checks for frequency and voltage margin */
#define FREQ_MARGIN_MAX    300.0F
#define FREQ_MARGIN_MIN    -300.0F
#define VOLTAGE_MARGIN_MAX 150.0F
#define VOLTAGE_MARGIN_MIN -150.0F

static float freq_margin_mhz;
static float voltage_margin_mv;

void InitVFCurve(void)
{
	freq_margin_mhz = CLAMP(get_fw_table()->chip_limits.frequency_margin, FREQ_MARGIN_MIN,
				FREQ_MARGIN_MAX);
	voltage_margin_mv = CLAMP(get_fw_table()->chip_limits.voltage_margin, VOLTAGE_MARGIN_MIN,
				  VOLTAGE_MARGIN_MAX);
}

/**
 * @brief Calculate the voltage based on the frequency
 *
 * @param freq_mhz The frequency in MHz
 * @return The voltage in mV
 */
float VFCurve(float freq_mhz)
{
	float freq_with_margin_mhz = freq_mhz + freq_margin_mhz;
	float voltage_mv = 0.00031395F * freq_with_margin_mhz * freq_with_margin_mhz -
			   0.43953F * freq_with_margin_mhz + 828.83F;

	return voltage_mv + voltage_margin_mv;
}
