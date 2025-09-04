/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "reg.h"
#include "telemetry.h"
#include "timer.h"

#include <float.h> /* for FLT_MAX */

#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/post_code.h>
#include <tenstorrent/sys_init_defines.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/tenstorrent/pvt_tt_bh.h>

#ifdef CONFIG_DT_HAS_TENSTORRENT_BH_PVT_ENABLED

static const struct device *const pvt = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pvt));

SENSOR_DT_READ_IODEV(vm_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_VM, 0},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 1}, {SENSOR_CHAN_PVT_TT_BH_VM, 2},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 3}, {SENSOR_CHAN_PVT_TT_BH_VM, 4},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 5}, {SENSOR_CHAN_PVT_TT_BH_VM, 6},
		     {SENSOR_CHAN_PVT_TT_BH_VM, 7});

SENSOR_DT_READ_IODEV(ts_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_TS, 0},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 1}, {SENSOR_CHAN_PVT_TT_BH_TS, 2},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 3}, {SENSOR_CHAN_PVT_TT_BH_TS, 4},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 5}, {SENSOR_CHAN_PVT_TT_BH_TS, 6},
		     {SENSOR_CHAN_PVT_TT_BH_TS, 7});

SENSOR_DT_READ_IODEV(pd_iodev, DT_NODELABEL(pvt), {SENSOR_CHAN_PVT_TT_BH_PD, 0},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 1}, {SENSOR_CHAN_PVT_TT_BH_PD, 2},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 3}, {SENSOR_CHAN_PVT_TT_BH_PD, 4},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 5}, {SENSOR_CHAN_PVT_TT_BH_PD, 6},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 7}, {SENSOR_CHAN_PVT_TT_BH_PD, 8},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 9}, {SENSOR_CHAN_PVT_TT_BH_PD, 10},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 11}, {SENSOR_CHAN_PVT_TT_BH_PD, 12},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 13}, {SENSOR_CHAN_PVT_TT_BH_PD, 14},
		     {SENSOR_CHAN_PVT_TT_BH_PD, 15});

RTIO_DEFINE(pvt_ctx, 16, 16);

/*
 * Used for storing raw temperature sensor data when reading.
 */
uint8_t buf[sizeof(struct sensor_value) * 8];

/* return selected TS raw reading and temperature in telemetry format */
static uint8_t read_ts_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	struct sensor_value celcius;
	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	ret = sensor_read(&ts_iodev, &pvt_ctx, buf, sizeof(buf));

	uint32_t id = request->data[1];

	decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_TS, id}, NULL, 8,
			&celcius);

	response->data[1] = temp_to_raw(&celcius);
	response->data[2] = ConvertFloatToTelemetry(sensor_value_to_float(&celcius));

	return ret;
}

/* return selected PD raw reading and frequency in telemetry format */
static uint8_t read_pd_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	struct sensor_value freq;
	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	ret = sensor_read(&pd_iodev, &pvt_ctx, buf, sizeof(buf));

	uint32_t delay_chain = request->data[1];

	pvt_tt_bh_delay_chain_set(delay_chain);

	uint32_t id = request->data[2];

	decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_PD, id}, NULL, 8,
			&freq);

	response->data[1] = freq_to_raw(&freq);
	response->data[2] = ConvertFloatToTelemetry(sensor_value_to_float(&freq));

	return ret;
}

/* return selected VM raw reading and voltage in mV */
static uint8_t read_vm_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	struct sensor_value volts;
	const struct sensor_decoder_api *decoder;
	int ret;

	ret = sensor_get_decoder(pvt, &decoder);
	ret = sensor_read(&vm_iodev, &pvt_ctx, buf, sizeof(buf));

	uint32_t id = request->data[1];

	decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PVT_TT_BH_VM, id}, NULL, 8,
			&volts);

	response->data[1] = volt_to_raw(&volts);
	response->data[2] = (uint16_t)sensor_value_to_float(&volts) * 1000;

	return ret;
}

REGISTER_MESSAGE(MSG_TYPE_READ_TS, read_ts_handler);
REGISTER_MESSAGE(MSG_TYPE_READ_PD, read_pd_handler);
REGISTER_MESSAGE(MSG_TYPE_READ_VM, read_vm_handler);

#endif
