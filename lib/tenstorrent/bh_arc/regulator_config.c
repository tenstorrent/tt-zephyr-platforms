/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

typedef struct {
	uint8_t cmd;
	const uint8_t *data;
	const uint8_t *mask;
	uint32_t size;
} RegulatorData;

#define REGULATOR_DATA(regulator, cmd) \
	{0x##cmd, regulator##_##cmd##_data, regulator##_##cmd##_mask, \
	sizeof(regulator##_##cmd##_data)}

/* VCORE */
static const uint8_t vcore_b0_data[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x02, 0x00, 0x00,
	0x11, 0x00, 0x00, 0x00,
	0x00, 0x41, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00};
static const uint8_t vcore_b0_mask[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0x00, 0x00,
	0x1f, 0x00, 0x00, 0x00,
	0x00, 0x7f, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00};

BUILD_ASSERT(sizeof(vcore_b0_data) == sizeof(vcore_b0_mask));

static const uint8_t vcore_cb_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t vcore_cb_mask[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(vcore_cb_data) == sizeof(vcore_cb_mask));

static const uint8_t vcore_d3_data[] = {0x00};
static const uint8_t vcore_d3_mask[] = {0x80};

BUILD_ASSERT(sizeof(vcore_d3_data) == sizeof(vcore_d3_mask));

static const uint8_t vcore_ca_data[] = {0x00, 0x78, 0x00, 0x00, 0x00};
static const uint8_t vcore_ca_mask[] = {0x00, 0xff, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(vcore_ca_data) == sizeof(vcore_ca_mask));

static const uint8_t vcore_38_data[] = {0x08, 0x00};
static const uint8_t vcore_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(vcore_38_data) == sizeof(vcore_38_mask));

static const uint8_t vcore_39_data[] = {0x0c, 0x00};
static const uint8_t vcore_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(vcore_39_data) == sizeof(vcore_39_mask));

static const uint8_t vcore_e7_data[] = {0x01};
static const uint8_t vcore_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(vcore_e7_data) == sizeof(vcore_e7_mask));

const RegulatorData vcore_data[] = {
	REGULATOR_DATA(vcore, b0),
	REGULATOR_DATA(vcore, cb),
	REGULATOR_DATA(vcore, d3),
	REGULATOR_DATA(vcore, ca),
	REGULATOR_DATA(vcore, 38),
	REGULATOR_DATA(vcore, 39),
	REGULATOR_DATA(vcore, e7),
};

/* VCOREM */
static const uint8_t vcorem_b0_data[] = {
	0x00, 0x00, 0x2b, 0x00,
	0x00, 0x07, 0x00, 0x00,
	0x09, 0x00, 0x09, 0x00,
	0x00, 0x00, 0x00, 0x00};
static const uint8_t vcorem_b0_mask[] = {
	0x00, 0x00, 0x3f, 0x00,
	0x00, 0x1f, 0x00, 0x00,
	0x1f, 0x00, 0x0f, 0x00,
	0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(vcorem_b0_data) == sizeof(vcorem_b0_mask));

static const uint8_t vcorem_38_data[] = {0x08, 0x00};
static const uint8_t vcorem_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(vcorem_38_data) == sizeof(vcorem_38_mask));

static const uint8_t vcorem_39_data[] = {0x0c, 0x00};
static const uint8_t vcorem_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(vcorem_39_data) == sizeof(vcorem_39_mask));

static const uint8_t vcorem_e7_data[] = {0x04};
static const uint8_t vcorem_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(vcorem_e7_data) == sizeof(vcorem_e7_mask));

const RegulatorData vcorem_data[] = {
	REGULATOR_DATA(vcorem, b0),
	REGULATOR_DATA(vcorem, 38),
	REGULATOR_DATA(vcorem, 39),
	REGULATOR_DATA(vcorem, e7),
};
