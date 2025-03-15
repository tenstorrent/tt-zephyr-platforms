/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include "vf_curve.h"

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
	float voltage_mv = 0.00031395F * freq_mhz * freq_mhz - 0.43953F * freq_mhz + 828.83F;

	return voltage_mv + 50.0F; /* Add 50 mV of margin */
}
