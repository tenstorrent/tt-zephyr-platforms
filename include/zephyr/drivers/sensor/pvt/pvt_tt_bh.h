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
	SENSOR_CHAN_PVT_TT_BH_TS = SENSOR_CHAN_PRIV_START,
	SENSOR_CHAN_PVT_TT_BH_VM,
	SENSOR_CHAN_PVT_TT_BH_PD,
};

typedef enum {
	ReadOk = 0,
	SampleFault = 1,
	IncorrectSampleType = 2,
	SdifTimeout = 3,
} ReadStatus;

#endif /* PVT_TT_BH_H */
