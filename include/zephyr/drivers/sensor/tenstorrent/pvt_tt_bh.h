/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PVT_TT_BH_H
#define PVT_TT_BH_H

#include <zephyr/drivers/sensor.h>

enum pvt_tt_bh_attribute {
	SENSOR_ATTR_PVT_TT_BH_NUM_PD = SENSOR_ATTR_PRIV_START,
	SENSOR_ATTR_PVT_TT_BH_NUM_VM,
	SENSOR_ATTR_PVT_TT_BH_NUM_TS,
};

/**
 * Information for each device will be stored as a `struct sensor_chan_spec`,
 * which contains the channel (TS, VM or PD) and the index.
 */
enum pvt_tt_bh_channel {
	SENSOR_CHAN_PVT_TT_BH_PD = SENSOR_CHAN_PRIV_START,
	SENSOR_CHAN_PVT_TT_BH_VM,
	SENSOR_CHAN_PVT_TT_BH_TS,
	SENSOR_CHAN_PVT_TT_BH_TS_AVG,
};

typedef enum {
	ReadOk = 0,
	SampleFault = 1,
	IncorrectSampleType = 2,
	SdifTimeout = 3,
} ReadStatus;

struct pvt_tt_bh_config {
	uint8_t num_pd;
	uint8_t num_vm;
	uint8_t num_ts;
};

struct pvt_tt_bh_data {
};

/*
 * Raw sensor data that will be submitted to the rtio buffer for the decoder
 * to then use.
 */
struct pvt_tt_bh_rtio_data {
	struct sensor_chan_spec spec;
	uint16_t raw; /* Raw sensor data can fit within 16 bits. */
};

/*
 * Convert raw temperature sensor data to celcius.
 */
float raw_to_temp(uint16_t raw);

/*
 * Convert celcius into raw temperature sensor data.
 */
uint16_t temp_to_raw(const struct sensor_value *value);

/*
 * Convert raw voltage monitor data to volts.
 */
float raw_to_volt(uint16_t raw);

/*
 * Convert voltage insto raw voltage monitor data;
 */
uint16_t volt_to_raw(const struct sensor_value *value);

/*
 * Convert raw process detector data to MHz.
 */
float raw_to_freq(uint16_t raw);

/*
 * Convert frequency into raw process detector data.
 */
uint16_t freq_to_raw(const struct sensor_value *value);

/*
 * Represent float data as two integers in struct sensor_value.
 */
void float_to_sensor_value(float data, struct sensor_value *val);

int pvt_tt_bh_get_decoder(const struct device *dev, const struct sensor_decoder_api **api);

void pvt_tt_bh_submit(const struct device *sensor, struct rtio_iodev_sqe *sqe);

void pvt_tt_bh_delay_chain_set(uint32_t new_delay_chain_);

#endif /* PVT_TT_BH_H */
