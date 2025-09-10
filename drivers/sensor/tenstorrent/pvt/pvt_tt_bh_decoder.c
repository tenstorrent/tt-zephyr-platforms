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

uint16_t pvt_tt_bh_temp_to_raw(const struct sensor_value *value)
{
	float temp = sensor_value_to_float(value);

	/* Reverse equation: temp = 83.09 + 262.5 * (raw / 4096 - 0.5) */
	float eqbs = (temp - 83.09f) / 262.5f;
	float raw_f = (eqbs + 0.5f) * 4096.0f;

	/* Clamp to valid 16-bit range */
	if (raw_f < 0.0f) {
		raw_f = 0.0f;
	}
	if (raw_f > 65535.0f) {
		raw_f = 65535.0f;
	}

	return (uint16_t)(raw_f + 0.5f);
}

float pvt_tt_bh_raw_to_volt(uint16_t raw)
{
	float k1 = VM_VREF * 6 / (5 * 16384);
	float offset = VM_VREF / 5 * (3 / 256 + 1);

	return k1 * raw - offset;
}

uint16_t pvt_tt_bh_volt_to_raw(const struct sensor_value *value)
{
	float volt = sensor_value_to_float(value);

	float k1 = VM_VREF * 6 / (5 * 16384);
	float offset = VM_VREF / 5 * (3.0f / 256.0f + 1.0f);

	float raw_f = (volt + offset) / k1;

	if (raw_f < 0.0f) {
		raw_f = 0.0f;
	}
	if (raw_f > 65535.0f) {
		raw_f = 65535.0f;
	}

	return (uint16_t)(raw_f + 0.5f);
}

float pvt_tt_bh_raw_to_freq(uint16_t raw)
{
	float a = 4.0;
	float b = 1.0;
	float w = 255.0;
	float fclk = 5.0;

	return raw * a * b * fclk / w;
}

uint16_t pvt_tt_bh_freq_to_raw(const struct sensor_value *value)
{
	float freq = sensor_value_to_float(value);

	float a = 4.0f;
	float b = 1.0f;
	float w = 255.0f;
	float fclk = 5.0f;

	float raw_f = freq * w / (a * b * fclk);

	if (raw_f < 0.0f) {
		raw_f = 0.0f;
	}
	if (raw_f > 65535.0f) {
		raw_f = 65535.0f;
	}

	return (uint16_t)(raw_f + 0.5f);
}

void pvt_tt_bh_float_to_sensor_value(float data, struct sensor_value *val)
{
	val->val1 = (int32_t)data;
	val->val2 = (int32_t)roundf((data - (float)val->val1) * 1000000.0f);

	/* Handle carry/borrow if val2 rounded to 1e6 or -1e6 */
	if (val->val2 >= 1000000) {
		val->val1 += 1;
		val->val2 -= 1000000;
	} else if (val->val2 <= -1000000) {
		val->val1 -= 1;
		val->val2 += 1000000;
	}
}

static int pvt_tt_bh_decode_sample(const uint8_t *buf, struct sensor_chan_spec chan_spec,
				   uint32_t *fit, uint16_t max_count, void *data_out)
{
	struct sensor_value *out = data_out;
	const struct pvt_tt_bh_rtio_data *data;
	float data_converted = 0;

	for (int i = 0; i < max_count; ++i) {
		data = &(((const struct pvt_tt_bh_rtio_data *)buf)[i]);
		if (data->spec.chan_type != chan_spec.chan_type ||
		    data->spec.chan_idx != chan_spec.chan_idx) {
			continue;
		}

		switch (chan_spec.chan_type) {
		case SENSOR_CHAN_PVT_TT_BH_PD: {
			data_converted = pvt_tt_bh_raw_to_freq(data->raw);
			break;
		}
		case SENSOR_CHAN_PVT_TT_BH_VM: {
			data_converted = pvt_tt_bh_raw_to_volt(data->raw);
			break;
		}
		case SENSOR_CHAN_PVT_TT_BH_TS:
		case SENSOR_CHAN_PVT_TT_BH_TS_AVG: {
			data_converted = pvt_tt_bh_raw_to_temp(data->raw);
			break;
		}
		default:
			return -ENOTSUP;
		}

		break;
	}

	pvt_tt_bh_float_to_sensor_value(data_converted, out);
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
