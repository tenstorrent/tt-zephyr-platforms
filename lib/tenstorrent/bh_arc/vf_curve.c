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
	/* VF curve from a single typical part on a liquid-cooled Orion-CB */
	freq_mhz = MAX(937.5F, freq_mhz); /* This curve is only valid above 937.5 MHz */
	float voltage_mv = 3.63763e-4F * freq_mhz * freq_mhz - 6.48668e-1F * freq_mhz + 9.60745e2F;

	return voltage_mv + 100.0F; /* Add 100 mV of margin */
}
