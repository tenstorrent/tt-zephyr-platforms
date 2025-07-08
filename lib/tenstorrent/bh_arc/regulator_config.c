/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/sys/util.h>
#include "regulator.h"
#include "regulator_config.h"

#define REGULATOR_DATA(regulator, cmd) \
	{0x##cmd, regulator##_##cmd##_data, regulator##_##cmd##_mask, \
	sizeof(regulator##_##cmd##_data)}

/* VCORE */
static const uint8_t p1x0_vcore_b0_data[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x02, 0x00, 0x00,
	0x11, 0x00, 0x00, 0x00,
	0x00, 0x41, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00};
static const uint8_t p1x0_vcore_b0_mask[] = {
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x1f, 0x00, 0x00,
	0x1f, 0x00, 0x00, 0x00,
	0x00, 0x7f, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x00};

BUILD_ASSERT(sizeof(p1x0_vcore_b0_data) == sizeof(p1x0_vcore_b0_mask));

static const uint8_t p1x0_vcore_cb_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p1x0_vcore_cb_mask[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p1x0_vcore_cb_data) == sizeof(p1x0_vcore_cb_mask));

static const uint8_t p1x0_vcore_d3_data[] = {0x00};
static const uint8_t p1x0_vcore_d3_mask[] = {0x80};

BUILD_ASSERT(sizeof(p1x0_vcore_d3_data) == sizeof(p1x0_vcore_d3_mask));

static const uint8_t p1x0_vcore_ca_data[] = {0x00, 0x78, 0x00, 0x00, 0x00};
static const uint8_t p1x0_vcore_ca_mask[] = {0x00, 0xff, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p1x0_vcore_ca_data) == sizeof(p1x0_vcore_ca_mask));

static const uint8_t p1x0_vcore_38_data[] = {0x08, 0x00};
static const uint8_t p1x0_vcore_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p1x0_vcore_38_data) == sizeof(p1x0_vcore_38_mask));

static const uint8_t p1x0_vcore_39_data[] = {0x0c, 0x00};
static const uint8_t p1x0_vcore_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p1x0_vcore_39_data) == sizeof(p1x0_vcore_39_mask));

static const uint8_t p1x0_vcore_e7_data[] = {0x01};
static const uint8_t p1x0_vcore_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(p1x0_vcore_e7_data) == sizeof(p1x0_vcore_e7_mask));

static const RegulatorData p1x0_vcore_data[] = {
	REGULATOR_DATA(p1x0_vcore, b0),
	REGULATOR_DATA(p1x0_vcore, cb),
	REGULATOR_DATA(p1x0_vcore, d3),
	REGULATOR_DATA(p1x0_vcore, ca),
	REGULATOR_DATA(p1x0_vcore, 38),
	REGULATOR_DATA(p1x0_vcore, 39),
	REGULATOR_DATA(p1x0_vcore, e7),
};

/* VCOREM */
static const uint8_t p1x0_vcorem_b0_data[] = {
	0x00, 0x00, 0x2b, 0x00,
	0x00, 0x07, 0x00, 0x00,
	0x09, 0x00, 0x09, 0x00,
	0x00, 0x00, 0x00, 0x00};
static const uint8_t p1x0_vcorem_b0_mask[] = {
	0x00, 0x00, 0x3f, 0x00,
	0x00, 0x1f, 0x00, 0x00,
	0x1f, 0x00, 0x0f, 0x00,
	0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p1x0_vcorem_b0_data) == sizeof(p1x0_vcorem_b0_mask));

static const uint8_t p1x0_vcorem_38_data[] = {0x08, 0x00};
static const uint8_t p1x0_vcorem_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p1x0_vcorem_38_data) == sizeof(p1x0_vcorem_38_mask));

static const uint8_t p1x0_vcorem_39_data[] = {0x0c, 0x00};
static const uint8_t p1x0_vcorem_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p1x0_vcorem_39_data) == sizeof(p1x0_vcorem_39_mask));

static const uint8_t p1x0_vcorem_e7_data[] = {0x04};
static const uint8_t p1x0_vcorem_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(p1x0_vcorem_e7_data) == sizeof(p1x0_vcorem_e7_mask));

static const RegulatorData p1x0_vcorem_data[] = {
	REGULATOR_DATA(p1x0_vcorem, b0),
	REGULATOR_DATA(p1x0_vcorem, 38),
	REGULATOR_DATA(p1x0_vcorem, 39),
	REGULATOR_DATA(p1x0_vcorem, e7),
};

static const uint8_t serdes_vr_d2_data[] = {0x07};
static const uint8_t serdes_vr_d2_mask[] = {0xff};

BUILD_ASSERT(sizeof(serdes_vr_d2_data) == sizeof(serdes_vr_d2_mask));

static const RegulatorData serdes_vr_data[] = {
	REGULATOR_DATA(serdes_vr, d2),
};

static const RegulatorConfig p1x0_config[] = {
	{
		.address = P0V8_VCORE_ADDR,
		.regulator_data = p1x0_vcore_data,
		.count = ARRAY_SIZE(p1x0_vcore_data),
	},
	{
		.address = P0V8_VCOREM_ADDR,
		.regulator_data = p1x0_vcorem_data,
		.count = ARRAY_SIZE(p1x0_vcorem_data),
	},
	{
		.address = SERDES_VDDL_ADDR,
		.regulator_data = serdes_vr_data,
		.count = ARRAY_SIZE(serdes_vr_data),
	},
	{
		.address = SERDES_VDD_ADDR,
		.regulator_data = serdes_vr_data,
		.count = ARRAY_SIZE(serdes_vr_data),
	},
	{
		.address = SERDES_VDDH_ADDR,
		.regulator_data = serdes_vr_data,
		.count = ARRAY_SIZE(serdes_vr_data),
	},
};

const BoardRegulatorsConfig p1x0_regulators_config = {
	.regulator_config = p1x0_config,
	.count = ARRAY_SIZE(p1x0_config),
};
