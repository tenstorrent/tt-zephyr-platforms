/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REGULATOR_CONFIG_H
#define REGULATOR_CONFIG_H

#include <stdint.h>

typedef struct {
	uint8_t cmd;
	const uint8_t *data;
	const uint8_t *mask;
	uint32_t size;
} RegulatorData;

typedef struct {
	uint8_t address;
	const RegulatorData *regulator_data;
	uint32_t count;
} RegulatorConfig;

typedef struct {
	const RegulatorConfig *regulator_config;
	uint32_t count;
} BoardRegulatorsConfig;

extern const BoardRegulatorsConfig p150_regulators_config;
extern const BoardRegulatorsConfig p300_left_regulators_config;
extern const BoardRegulatorsConfig p300_right_regulators_config;
extern const BoardRegulatorsConfig ubb_regulators_config;

#endif
