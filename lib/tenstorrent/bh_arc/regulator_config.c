/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <zephyr/sys/util.h>
#include "regulator.h"
#include "regulator_config.h"

#define REGULATOR_DATA(regulator, cmd)                                                             \
	{0x##cmd, regulator##_##cmd##_data, regulator##_##cmd##_mask,                              \
	 sizeof(regulator##_##cmd##_data)}

/***** Start of p1x0 (p150 / p100) settings */
/* VCORE */
static const uint8_t p1x0_vcore_b0_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
					     0x11, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00,
					     0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p1x0_vcore_b0_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00,
					     0x1f, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00,
					     0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

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
	REGULATOR_DATA(p1x0_vcore, b0), REGULATOR_DATA(p1x0_vcore, cb),
	REGULATOR_DATA(p1x0_vcore, d3), REGULATOR_DATA(p1x0_vcore, ca),
	REGULATOR_DATA(p1x0_vcore, 38), REGULATOR_DATA(p1x0_vcore, 39),
	REGULATOR_DATA(p1x0_vcore, e7),
};

/* VCOREM */
static const uint8_t p1x0_vcorem_b0_data[] = {0x00, 0x00, 0x2b, 0x00, 0x00, 0x07, 0x00, 0x00,
					      0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p1x0_vcorem_b0_mask[] = {0x00, 0x00, 0x3f, 0x00, 0x00, 0x1f, 0x00, 0x00,
					      0x1f, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00};

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

/***** End of p1x0 settings */

/***** Start of p300 settings */

/* VCORE */
static const uint8_t p300_vcore_b0_data[] = {0x00, 0x00, 0x3c, 0x00, 0x00, 0x03, 0x00, 0x00,
					     0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					     0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p300_vcore_b0_mask[] = {0x00, 0x00, 0x3f, 0x00, 0x00, 0x1f, 0x00, 0x00,
					     0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
					     0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p300_vcore_b0_data) == sizeof(p300_vcore_b0_mask));

static const uint8_t p300_vcore_cb_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p300_vcore_cb_mask[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p300_vcore_cb_data) == sizeof(p300_vcore_cb_mask));

static const uint8_t p300_vcore_38_data[] = {0x02, 0x00};
static const uint8_t p300_vcore_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p300_vcore_38_data) == sizeof(p300_vcore_38_mask));

static const uint8_t p300_vcore_39_data[] = {0x02, 0x00};
static const uint8_t p300_vcore_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p300_vcore_39_data) == sizeof(p300_vcore_39_mask));

static const uint8_t p300_vcore_e7_data[] = {0x01};
static const uint8_t p300_vcore_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(p300_vcore_e7_data) == sizeof(p300_vcore_e7_mask));

static const RegulatorData p300_vcore_data[] = {
	REGULATOR_DATA(p300_vcore, b0), REGULATOR_DATA(p300_vcore, cb),
	REGULATOR_DATA(p300_vcore, 38), REGULATOR_DATA(p300_vcore, 39),
	REGULATOR_DATA(p300_vcore, e7),
};

/* VCOREM */
static const uint8_t p300_vcorem_b0_data[] = {0x00, 0x00, 0x2b, 0x00, 0x00, 0x07, 0x00, 0x00,
					      0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t p300_vcorem_b0_mask[] = {0x00, 0x00, 0x3f, 0x00, 0x00, 0x1f, 0x00, 0x00,
					      0x1f, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(p300_vcorem_b0_data) == sizeof(p300_vcorem_b0_mask));

static const uint8_t p300_vcorem_38_data[] = {0x08, 0x00};
static const uint8_t p300_vcorem_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p300_vcorem_38_data) == sizeof(p300_vcorem_38_mask));

static const uint8_t p300_vcorem_39_data[] = {0x0c, 0x00};
static const uint8_t p300_vcorem_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(p300_vcorem_39_data) == sizeof(p300_vcorem_39_mask));

static const uint8_t p300_vcorem_e7_data[] = {0x04};
static const uint8_t p300_vcorem_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(p300_vcorem_e7_data) == sizeof(p300_vcorem_e7_mask));

static const RegulatorData p300_vcorem_data[] = {
	REGULATOR_DATA(p300_vcorem, b0),
	REGULATOR_DATA(p300_vcorem, 38),
	REGULATOR_DATA(p300_vcorem, 39),
	REGULATOR_DATA(p300_vcorem, e7),
};

/***** End of p300 settings */

/***** Start of Galaxy/UBB settings */

/* VCORE */
static const uint8_t ubb_vcore_b0_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x00,
					    0x11, 0x00, 0x00, 0x00, 0x00, 0x41, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ubb_vcore_b0_mask[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x0f, 0x00,
					    0x1f, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00,
					    0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(ubb_vcore_b0_data) == sizeof(ubb_vcore_b0_mask));

static const uint8_t ubb_vcore_cb_data[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ubb_vcore_cb_mask[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(ubb_vcore_cb_data) == sizeof(ubb_vcore_cb_mask));

static const uint8_t ubb_vcore_d3_data[] = {0x00};
static const uint8_t ubb_vcore_d3_mask[] = {0x80};

BUILD_ASSERT(sizeof(ubb_vcore_d3_data) == sizeof(ubb_vcore_d3_mask));

static const uint8_t ubb_vcore_ca_data[] = {0x00, 0x78, 0x00, 0x00, 0x00};
static const uint8_t ubb_vcore_ca_mask[] = {0x00, 0xff, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(ubb_vcore_ca_data) == sizeof(ubb_vcore_ca_mask));

static const uint8_t ubb_vcore_38_data[] = {0x02, 0x00};
static const uint8_t ubb_vcore_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(ubb_vcore_38_data) == sizeof(ubb_vcore_38_mask));

static const uint8_t ubb_vcore_39_data[] = {0x02, 0x00};
static const uint8_t ubb_vcore_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(ubb_vcore_39_data) == sizeof(ubb_vcore_39_mask));

static const uint8_t ubb_vcore_e7_data[] = {0x01};
static const uint8_t ubb_vcore_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(ubb_vcore_e7_data) == sizeof(ubb_vcore_e7_mask));

static const RegulatorData ubb_vcore_data[] = {
	REGULATOR_DATA(ubb_vcore, b0), REGULATOR_DATA(ubb_vcore, cb), REGULATOR_DATA(ubb_vcore, d3),
	REGULATOR_DATA(ubb_vcore, ca), REGULATOR_DATA(ubb_vcore, 38), REGULATOR_DATA(ubb_vcore, 39),
	REGULATOR_DATA(ubb_vcore, e7),
};

/* VCOREM */
static const uint8_t ubb_vcorem_b0_data[] = {0x00, 0x00, 0x2b, 0x00, 0x00, 0x07, 0x00, 0x00,
					     0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t ubb_vcorem_b0_mask[] = {0x00, 0x00, 0x3f, 0x00, 0x00, 0x1f, 0x00, 0x00,
					     0x1f, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00};

BUILD_ASSERT(sizeof(ubb_vcorem_b0_data) == sizeof(ubb_vcorem_b0_mask));

static const uint8_t ubb_vcorem_38_data[] = {0x02, 0x00};
static const uint8_t ubb_vcorem_38_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(ubb_vcorem_38_data) == sizeof(ubb_vcorem_38_mask));

static const uint8_t ubb_vcorem_39_data[] = {0x02, 0x00};
static const uint8_t ubb_vcorem_39_mask[] = {0xff, 0x07};

BUILD_ASSERT(sizeof(ubb_vcorem_39_data) == sizeof(ubb_vcorem_39_mask));

static const uint8_t ubb_vcorem_e7_data[] = {0x04};
static const uint8_t ubb_vcorem_e7_mask[] = {0x07};

BUILD_ASSERT(sizeof(ubb_vcorem_e7_data) == sizeof(ubb_vcorem_e7_mask));

static const RegulatorData ubb_vcorem_data[] = {
	REGULATOR_DATA(ubb_vcorem, b0),
	REGULATOR_DATA(ubb_vcorem, 38),
	REGULATOR_DATA(ubb_vcorem, 39),
	REGULATOR_DATA(ubb_vcorem, e7),
};

/* VOUT_SCALE_LOOP (0x29) = 444 (write word) */
static const uint8_t ubb_gddrio_29_data[] = {0xbc, 0x01};
static const uint8_t ubb_gddrio_29_mask[] = {0xff, 0xff};

BUILD_ASSERT(sizeof(ubb_gddrio_29_data) == sizeof(ubb_gddrio_29_mask));

/* VOUT_CMD (0x21) = 675 (write word) */
static const uint8_t ubb_gddrio_21_data[] = {0xa3, 0x02};
static const uint8_t ubb_gddrio_21_mask[] = {0xff, 0xff};

BUILD_ASSERT(sizeof(ubb_gddrio_21_data) == sizeof(ubb_gddrio_21_mask));

static const RegulatorData ubb_gddrio_data[] = {
	REGULATOR_DATA(ubb_gddrio, 29),
	REGULATOR_DATA(ubb_gddrio, 21),
};

/***** End of Galaxy/UBB settings */

/***** Start of common serdes VR settings */

static const uint8_t serdes_vr_d2_data[] = {0x07};
static const uint8_t serdes_vr_d2_mask[] = {0xff};

BUILD_ASSERT(sizeof(serdes_vr_d2_data) == sizeof(serdes_vr_d2_mask));

static const RegulatorData serdes_vr_data[] = {
	REGULATOR_DATA(serdes_vr, d2),
};

/***** End of common serdes VR settings */

static const RegulatorConfig p150_config[] = {
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

const BoardRegulatorsConfig p150_regulators_config = {
	.regulator_config = p150_config,
	.count = ARRAY_SIZE(p150_config),
};

static const RegulatorConfig p300_left_config[] = {
	{
		.address = P0V8_VCORE_ADDR,
		.regulator_data = p300_vcore_data,
		.count = ARRAY_SIZE(p300_vcore_data),
	},
	{
		.address = P0V8_VCOREM_ADDR,
		.regulator_data = p300_vcorem_data,
		.count = ARRAY_SIZE(p300_vcorem_data),
	},
	{
		.address = SERDES_VDDL_ADDR,
		.regulator_data = serdes_vr_data,
		.count = ARRAY_SIZE(serdes_vr_data),
	},
	/* Left chip doesn't have its own SERDES_VDD */
	{
		.address = SERDES_VDDH_ADDR,
		.regulator_data = serdes_vr_data,
		.count = ARRAY_SIZE(serdes_vr_data),
	},
};

const BoardRegulatorsConfig p300_left_regulators_config = {
	.regulator_config = p300_left_config,
	.count = ARRAY_SIZE(p300_left_config),
};

static const RegulatorConfig p300_right_config[] = {
	{
		.address = P0V8_VCORE_ADDR,
		.regulator_data = p300_vcore_data,
		.count = ARRAY_SIZE(p300_vcore_data),
	},
	{
		.address = P0V8_VCOREM_ADDR,
		.regulator_data = p300_vcorem_data,
		.count = ARRAY_SIZE(p300_vcorem_data),
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

const BoardRegulatorsConfig p300_right_regulators_config = {
	.regulator_config = p300_right_config,
	.count = ARRAY_SIZE(p300_right_config),
};

static const RegulatorConfig ubb_config[] = {
	{
		.address = P0V8_VCORE_ADDR,
		.regulator_data = ubb_vcore_data,
		.count = ARRAY_SIZE(ubb_vcore_data),
	},
	{
		.address = P0V8_VCOREM_ADDR,
		.regulator_data = ubb_vcorem_data,
		.count = ARRAY_SIZE(ubb_vcorem_data),
	},
	{
		.address = GDDRIO_WEST_ADDR,
		.regulator_data = ubb_gddrio_data,
		.count = ARRAY_SIZE(ubb_gddrio_data),
	},
	{
		.address = GDDRIO_EAST_ADDR,
		.regulator_data = ubb_gddrio_data,
		.count = ARRAY_SIZE(ubb_gddrio_data),
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

const BoardRegulatorsConfig ubb_regulators_config = {
	.regulator_config = ubb_config,
	.count = ARRAY_SIZE(ubb_config),
};
