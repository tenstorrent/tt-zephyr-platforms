/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/fff.h>
#include <tenstorrent/tt_smbus_regs.h>
#include "reg_mock.h"

extern struct i2c_target_config i2c_target_config_impl;

static int write_received(uint8_t val)
{
	return i2c_target_config_impl.callbacks->write_received(&i2c_target_config_impl, val);
}
static int read_received(uint8_t *val)
{
	return i2c_target_config_impl.callbacks->read_requested(&i2c_target_config_impl, val);
}

static int stop(void)
{
	return i2c_target_config_impl.callbacks->stop(&i2c_target_config_impl);
}

static void tear_down_tc(void *fixture)
{
	(void)fixture;
	stop();
}

ZTEST(smbus_target, test_write_received_bad_cmd_0)
{
	/* 0 is not a valid command */
	zassert_equal(-1, write_received(0U));
}

ZTEST(smbus_target, test_write_received_bad_cmd_255)
{
	/* 255 is not a valid command */
	zassert_equal(-1, write_received(255U));
}

ZTEST(smbus_target, test_write_received_bad_cmd_msg_max)
{
	/* 255 is not a valid command */
	zassert_equal(-1, write_received(CMFW_SMBUS_MSG_MAX));
}

ZTEST(smbus_target, test_write_received_cmd_before_stop)
{
	/* 0 is not a valid command */
	zassert_equal(-1, write_received(0U));

	/* starting a valid command before stopping the invalid command should fail */
	zassert_equal(-1, write_received(CMFW_SMBUS_TEST_WRITE_BLOCK));
}

ZTEST(smbus_target, test_unsolicited_read_received)
{
	uint8_t dc;

	/* We always need to get a command to process first. */
	zassert_equal(-1, read_received(&dc));
	zassert_equal(0xFF, dc);
}

ZTEST(smbus_target, test_write_received_bad_blocksize)
{
	/* WRITE_BLOCK is a valid command */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_WRITE_BLOCK));

	/* WRITE_BLOCK expects a blocksize of 4 to be received, so 5 should fail */
	zassert_equal(-1, write_received(5));
}

ZTEST(smbus_target, test_write_received_data_for_read_cmd)
{
	/* TEST_READ is a valid command but expects the user to write no additional data */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_READ));
}

ZTEST(smbus_target, test_write_rx_rqst_for_write_cmd)
{
	uint8_t err;

	/* TEST_READ is a valid command but expects data to be written, not read */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_WRITE_BLOCK));
	zassert_equal(-1, read_received(&err));
	zassert_equal(0xFF, err);
}

ZTEST(smbus_target, test_read_byte_test)
{
	uint8_t var;
	uint8_t pec;

	ReadReg_fake.return_val = 0x5AU;
	/* The value received by var should match what the fake stored */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_READ));
	zassert_equal(0, read_received(&var));
	zassert_equal(0, read_received(&pec));
	zassert_equal(0x5AU, var);
}

ZTEST(smbus_target, test_read_word_test)
{
	uint8_t var[2];
	uint8_t pec;

	ReadReg_fake.return_val = 0x915AU;
	/* The value received by var should match what the fake stored */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_READ_WORD));
	zassert_equal(0, read_received(&var[0]));
	zassert_equal(0, read_received(&var[1]));
	zassert_equal(0, read_received(&pec));
	zassert_equal(0x5AU, var[0]);
	zassert_equal(0x91U, var[1]);
}

ZTEST(smbus_target, test_read_block_test)
{
	uint8_t var[4];
	uint8_t size;
	uint8_t pec;

	ReadReg_fake.return_val = 0x8765915AU;
	/* The value received by var should match what the fake stored */
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_READ_BLOCK));
	zassert_equal(0, read_received(&size));
	zassert_equal(0, read_received(&var[0]));
	zassert_equal(0, read_received(&var[1]));
	zassert_equal(0, read_received(&var[2]));
	zassert_equal(0, read_received(&var[3]));
	zassert_equal(0, read_received(&pec));
	zassert_equal(4U, size);
	zassert_equal(0x5AU, var[0]);
	zassert_equal(0x91U, var[1]);
	zassert_equal(0x65U, var[2]);
	zassert_equal(0x87U, var[3]);
}

ZTEST(smbus_target, test_write_byte_test)
{
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_WRITE));
	zassert_equal(0, write_received(0x5BU));
	zassert_equal(0, write_received(136U));
	zassert_equal(WriteReg_fake.arg1_val, (1 << 16U) | 0x5BU);
}

ZTEST(smbus_target, test_write_word_test)
{
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_WRITE_WORD));
	zassert_equal(0, write_received(0x5BU));
	zassert_equal(0, write_received(0x2AU));
	zassert_equal(0, write_received(177U));
	zassert_equal(WriteReg_fake.arg1_val, (2 << 16U) | 0x2A5BU);
}

ZTEST(smbus_target, test_write_block_test)
{
	zassert_equal(0, write_received(CMFW_SMBUS_TEST_WRITE_BLOCK));
	zassert_equal(0, write_received(0x4U));
	zassert_equal(0, write_received(0x5BU));
	zassert_equal(0, write_received(0x2AU));
	zassert_equal(0, write_received(0x13U));
	zassert_equal(0, write_received(0x99U));
	zassert_equal(0, write_received(243U));
	zassert_equal(WriteReg_fake.arg1_val, 0x99132A5BU);
}

ZTEST_SUITE(smbus_target, NULL, NULL, NULL, tear_down_tc, NULL);
