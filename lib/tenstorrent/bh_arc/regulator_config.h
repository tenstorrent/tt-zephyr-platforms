/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REGULATOR_CONFIG_H
#define REGULATOR_CONFIG_H

#include <stdint.h>

struct regulator_data {
	uint8_t cmd;
	const uint8_t *data;
	const uint8_t *mask;
	uint32_t size;
};

struct regulator_config {
	uint8_t address;
	const struct regulator_data *regulator_data;
	uint32_t count;
};

struct board_regulators_config {
	const struct regulator_config *regulator_config;
	uint32_t count;
};

extern const struct board_regulators_config p150_regulators_config;
extern const struct board_regulators_config p300_left_regulators_config;
extern const struct board_regulators_config p300_right_regulators_config;
extern const struct board_regulators_config ubb_regulators_config;

#endif
