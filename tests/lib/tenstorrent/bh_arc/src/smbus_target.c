/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/fff.h>
#include <tenstorrent/tt_smbus_regs.h>
#include <zephyr/drivers/i2c.h>
#include "reg_mock.h"
#include "asic_state.h"
#include "telemetry.h"
#include "status_reg.h"
#include "cm2dm_msg.h"

static const struct device *const i2c0_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c0));
static const uint8_t tt_i2c_addr = 0xA;
static void tear_down_tc(void *fixture)
{
	(void)fixture;
	extern struct i2c_target_config i2c_target_config_impl;

	i2c_target_config_impl.callbacks->stop(&i2c_target_config_impl);
}

ZTEST(smbus_target, test_write_received_bad_cmd_0)
{
	/* 0 is not a valid command */
	uint8_t write_data[] = {0U};

	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_write_received_bad_cmd_255)
{
	/* 255 is not a valid command */
	uint8_t write_data[] = {255U};

	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_write_received_bad_cmd_msg_max)
{
	uint8_t write_data[] = {CMFW_SMBUS_MSG_MAX};

	/* 255 is not a valid command */
	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_write_received_cmd_before_stop)
{
	uint8_t write_data[] = {0, CMFW_SMBUS_MSG_MAX};

	/* starting a valid command before stopping the invalid command should fail */
	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_unsolicited_read_received)
{
	uint8_t read_data[1];

	/* We always need to get a command to process first. */
	zassert_equal(-1, i2c_read(i2c0_dev, read_data, sizeof(read_data), tt_i2c_addr));
	zexpect_equal(0xFF, read_data[0]);
}

ZTEST(smbus_target, test_write_received_bad_blocksize)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_WRITE_BLOCK, 5U};

	/* WRITE_BLOCK is a valid command */
	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_write_received_data_for_read_cmd)
{
	/* TEST_READ is a valid command but expects the user to write no additional data */
	uint8_t write_data[] = {CMFW_SMBUS_TEST_READ, 10U};

	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_write_rx_rqst_for_write_cmd)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_WRITE_BLOCK, 4U};
	uint8_t read_data[1];
	/* TEST_WRITE_BLOCK is a valid command but expects data to be written, not read */
	zassert_equal(-1, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					 read_data, sizeof(read_data)));
	zexpect_equal(0xFF, read_data[0]);
}

ZTEST(smbus_target, test_read_byte_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_READ};
	uint8_t read_data[2];

	ReadReg_fake.return_val = 0x5AU;
	/* The value received by var should match what the fake stored */
	zassert_equal(0, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					read_data, sizeof(read_data)));
	zexpect_equal(0x5AU, read_data[0]);
}

ZTEST(smbus_target, test_read_word_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_READ_WORD};
	uint8_t read_data[3];

	ReadReg_fake.return_val = 0x915AU;

	/* The value received by var should match what the fake stored */
	zassert_equal(0, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					read_data, sizeof(read_data)));
	zexpect_equal(0x5AU, read_data[0]);
	zexpect_equal(0x91U, read_data[1]);
}

ZTEST(smbus_target, test_read_block_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_READ_BLOCK};
	uint8_t read_data[6];

	ReadReg_fake.return_val = 0x8765915AU;
	/* The value received by var should match what the fake stored */
	zassert_equal(0, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					read_data, sizeof(read_data)));
	zexpect_equal(4U, read_data[0]);
	zexpect_equal(0x5AU, read_data[1]);
	zexpect_equal(0x91U, read_data[2]);
	zexpect_equal(0x65U, read_data[3]);
	zexpect_equal(0x87U, read_data[4]);
}

ZTEST(smbus_target, test_write_byte_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_WRITE, 0x5BU, 136U};
	uint8_t set_count = 0U;

	zassert_equal(0, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
	for (uint8_t i = 0U; i < WriteReg_fake.call_count; i++) {
		if (WriteReg_fake.arg0_history[i] == STATUS_FW_SCRATCH_REG_ADDR) {
			zassert_equal(WriteReg_fake.arg1_history[i], (1 << 16U) | 0x5BU);
			set_count++;
		}
	}
	zexpect_equal(1U, set_count);
}

ZTEST(smbus_target, test_write_word_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_WRITE_WORD, 0x5BU, 0x2AU, 177U};
	uint8_t set_count = 0U;

	zassert_equal(0, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));

	for (uint8_t i = 0U; i < WriteReg_fake.call_count; i++) {
		if (WriteReg_fake.arg0_history[i] == STATUS_FW_SCRATCH_REG_ADDR) {
			zexpect_equal(WriteReg_fake.arg1_history[i], (2 << 16U) | 0x2A5BU);
			set_count++;
		}
	}
	zexpect_equal(1U, set_count);
}

ZTEST(smbus_target, test_write_block_test)
{
	uint8_t write_data[] = {CMFW_SMBUS_TEST_WRITE_BLOCK, 0x4, 0x5BU, 0x2AU, 0x13U, 0x99U, 243U};
	uint8_t set_count = 0U;

	zassert_equal(0, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));

	for (uint8_t i = 0U; i < WriteReg_fake.call_count; i++) {
		if (WriteReg_fake.arg0_history[i] == STATUS_FW_SCRATCH_REG_ADDR) {
			zexpect_equal(WriteReg_fake.arg1_history[i], 0x99132A5BU);
			set_count++;
		}
	}
	zexpect_equal(1U, set_count);
}

ZTEST(smbus_target, test_update_arc_test_state_3)
{
	uint8_t write_data[] = {CMFW_SMBUS_UPDATE_ARC_STATE, 0x3, 0x3U, 0xDEU, 0xAFU};

	zassert_equal(0, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
	zassert_equal(A3State, get_asic_state());
}

ZTEST(smbus_target, test_update_arc_test_state_0)
{
	uint8_t write_data[] = {CMFW_SMBUS_UPDATE_ARC_STATE, 0x3, 0x0U, 0xDEU, 0xAFU};

	zassert_equal(0, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
	zassert_equal(A0State, get_asic_state());
}

ZTEST(smbus_target, test_telem_read_bad_w_blocksize)
{
	uint8_t write_data[] = {CMFW_SMBUS_TELEMETRY_READ, 0x3};

	zassert_equal(-1, i2c_write(i2c0_dev, write_data, sizeof(write_data), tt_i2c_addr));
}

ZTEST(smbus_target, test_telem_read)
{
	uint8_t write_data[] = {CMFW_SMBUS_TELEMETRY_READ, 0x1U, TAG_AICLK};
	uint8_t read_data[8];

	zassert_equal(0, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					read_data, sizeof(read_data)));
	zexpect_equal(7U, read_data[0]);
	zexpect_equal(0U, read_data[1]);
	/* bytes 2-3 are DC. Bytes 4-7 are telem data but currently aren't emulated */
}

ZTEST(smbus_target, test_telem_write_no_reset)
{
	uint8_t write_data[35] = {[0] = CMFW_SMBUS_TELEMETRY_WRITE, [1] = 33U};
	uint8_t read_data[21];

	zassert_equal(0, i2c_write_read(i2c0_dev, tt_i2c_addr, write_data, sizeof(write_data),
					read_data, sizeof(read_data)));
	zexpect_equal(20U, read_data[0]);

	uint32_t ctl = 0;

	(void)memcpy(&ctl, &read_data[12], sizeof(ctl));
	zexpect_equal(0U, ctl);
}

ZTEST_SUITE(smbus_target, NULL, NULL, NULL, tear_down_tc, NULL);
