/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor/tenstorrent/pvt_tt_bh.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <math.h> /* roundf */

LOG_MODULE_DECLARE(pvt_tt_bh);

#define VM_VREF 1.2207f

float pvt_tt_bh_raw_to_temp(uint16_t raw)
{
	float eqbs = raw / 4096.0f - 0.5f;
	/* TODO: slope and offset need to be replaced with fused values */
	return 83.09f + 262.5f * eqbs;
}

float pvt_tt_bh_raw_to_volt(uint16_t raw)
{
	float k1 = VM_VREF * 6 / (5 * 16384);
	float offset = VM_VREF / 5 * (3 / 256 + 1);

	return k1 * raw - offset;
}

float pvt_tt_bh_raw_to_freq(uint16_t raw)
{
	float a = 4.0;
	float b = 1.0;
	float w = 255.0;
	float fclk = 5.0;

	return raw * a * b * fclk / w;
}

static int pvt_tt_bh_decode_sample(const uint8_t *buf, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	uint16_t *out = data_out;
	const struct pvt_tt_bh_rtio_data *data = (const struct pvt_tt_bh_rtio_data *)buf;

	for (int i = 0; i < max_count; ++i) {
		out[i] = data[i].raw;
	}

	return 0;
}

static int pvt_tt_bh_decoder_decode(const uint8_t *buf, struct sensor_chan_spec chan_spec,
				    uint32_t *fit, uint16_t max_count, void *data_out)
{
	return pvt_tt_bh_decode_sample(buf, chan_spec, fit, max_count, data_out);
}

SENSOR_DECODER_API_DT_DEFINE() = {
	.decode = pvt_tt_bh_decoder_decode,
};

int pvt_tt_bh_get_decoder(const struct device *dev, const struct sensor_decoder_api **decoder)
{
	ARG_UNUSED(dev);

	*decoder = &SENSOR_DECODER_NAME();
	return 0;
}
