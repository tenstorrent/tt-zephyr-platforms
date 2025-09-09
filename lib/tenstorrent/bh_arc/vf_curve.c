/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include "aiclk_ppm.h"
#include "vf_curve.h"
#include <zephyr/drivers/misc/bh_fwtable.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/smc_msg.h>

/* Bounds checks for frequency and voltage margin */
#define FREQ_MARGIN_MAX    300.0F
#define FREQ_MARGIN_MIN    -300.0F
#define VOLTAGE_MARGIN_MAX 150.0F
#define VOLTAGE_MARGIN_MIN -150.0F

static const float vf_quadratic_coeff = 0.00031395F;
static const float vf_linear_coeff = -0.43953F;
static const float vf_constant = 828.83F;

static float freq_margin_mhz = FREQ_MARGIN_MAX;
static float voltage_margin_mv = VOLTAGE_MARGIN_MAX;

static const struct device *const fwtable_dev = DEVICE_DT_GET(DT_NODELABEL(fwtable));

void InitVFCurve(void)
{
	freq_margin_mhz =
		CLAMP(tt_bh_fwtable_get_fw_table(fwtable_dev)->chip_limits.frequency_margin,
		      FREQ_MARGIN_MIN, FREQ_MARGIN_MAX);
	voltage_margin_mv =
		CLAMP(tt_bh_fwtable_get_fw_table(fwtable_dev)->chip_limits.voltage_margin,
		      VOLTAGE_MARGIN_MIN, VOLTAGE_MARGIN_MAX);
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
	float voltage_mv = vf_quadratic_coeff * freq_with_margin_mhz * freq_with_margin_mhz +
			   vf_linear_coeff * freq_with_margin_mhz + vf_constant;

	return voltage_mv + voltage_margin_mv;
}

static uint8_t get_voltage_curve_from_freq_handler(const union request *request,
						   struct response *response)
{
	float input_freq_mhz = (float)request->get_voltage_curve_from_freq.input_freq_mhz;
	float voltage_mv = VFCurve(input_freq_mhz);

	if (voltage_mv < 0.0F) {
		response->data[1] = 0U;
	} else {
		response->data[1] = (uint32_t)(voltage_mv);
	}

	return 0;
}

static uint8_t get_freq_curve_from_voltage_handler(const union request *request,
						   struct response *response)
{
	int input_voltage_mv = request->get_freq_curve_from_voltage.input_voltage_mv;
	int freq_mhz = GetMaxAiclkForVoltage(input_voltage_mv);

	response->data[1] = freq_mhz;

	return 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_GET_VOLTAGE_CURVE_FROM_FREQ, get_voltage_curve_from_freq_handler);
REGISTER_MESSAGE(TT_SMC_MSG_GET_FREQ_CURVE_FROM_VOLTAGE, get_freq_curve_from_voltage_handler);
