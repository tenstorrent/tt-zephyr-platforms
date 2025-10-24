/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include "asic_state.h"
#include "clock_wave.h"

#include "reg_mock.h"

/* Custom fake for ReadReg to simulate timer progression */
#define RESET_UNIT_REFCLK_CNT_LO_REG_ADDR 0x800300E0
#define PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR 0x80020038
static uint32_t timer_counter;
static uint8_t i2c_read_buf_emul[256] = {0};
static uint8_t i2c_read_buf_idx;

static uint8_t i2c_write_buf_emul[256] = {0};
static uint8_t i2c_write_buf_idx;

static uint32_t clock_wave_value;

static uint32_t ReadReg_msgqueue_fake(uint32_t addr)
{
	/* IC_STATUS; Fake out TX_FIFO to say empty and not full Fake out RX_FIFO to say not empty.
	 * This should be replaced by a emulated i2c driver once we use
	 * a real zephyr i2c controller in our app.
	 */
	if (addr == 0x80090070) {
		return 0b1110;
	}

	/* IC_DATA_CMD; Fake out RX data to provide emulated data*/
	if (addr == 0x80090010) {
		return i2c_read_buf_emul[i2c_read_buf_idx++];
	}

	if (addr == RESET_UNIT_REFCLK_CNT_LO_REG_ADDR) {
		return timer_counter++;
	}
	return 0;
}

static void WriteReg_msgqueue_fake(uint32_t addr, uint32_t value)
{
	/* IC_DATA_CMD; Fake out TX data to get test visibility on sent data
	 * Note the truncation; data to I2C is in the LSB
	 * More significant bytes of the value word contain i2c transaction flags
	 */
	if (addr == 0x80090010) {
		i2c_write_buf_emul[i2c_write_buf_idx++] = value;
	}

	if (addr == PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR) {
		clock_wave_value = value;
	}
}

static uint8_t msgqueue_handler_73(const union request *req, struct response *rsp)
{
	BUILD_ASSERT(MSG_TYPE_SHIFT % 8 == 0);
	rsp->data[1] = req->data[0];
	return 0;
}

ZTEST(msgqueue, test_msgqueue_register_handler)
{
	union request req = {0};
	struct response rsp = {0};

	msgqueue_register_handler(0x73, msgqueue_handler_73);

	req.data[0] = 0x73737373;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[1], 0x73737373);
}

ZTEST(msgqueue, test_msgqueue_power_settings_cmd)
{
	union request req = {0};
	struct response rsp = {0};

	/* LSB to MSB:
	 * 0x21: TT_SMC_MSG_POWER_SETTING
	 * 0x03: 3 power flags valid, 0 power settings valid
	 * 0x0003: max_ai_clk on, mrisc power on, tensix power off
	 */
	req.data[0] = 0x00030321;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0x0);
}

ZTEST(msgqueue, test_msg_type_set_voltage)
{
	union request req = {0};
	struct response rsp = {0};

	req.data[0] = TT_SMC_MSG_SET_VOLTAGE;
	req.data[1] = 0x64; /* regulator id */
	req.data[2] = 800;  /* voltage in mV */
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(i2c_write_buf_emul[0], 33); /*VOUT_COMMAND*/

	uint32_t received_voltage;

	memcpy(&received_voltage, &i2c_write_buf_emul[1], sizeof(received_voltage));

	zexpect_equal(received_voltage, 800 * 2);
}

ZTEST(msgqueue, test_msg_type_get_voltage)
{
	union request req = {0};
	struct response rsp = {0};

	/*Setup the simulated voltage for the i2c read*/
	uint32_t simulated_voltage_mv = 950;

	memcpy(i2c_read_buf_emul, &simulated_voltage_mv, sizeof(simulated_voltage_mv));

	req.data[0] = TT_SMC_MSG_GET_VOLTAGE;
	req.data[1] = 0x64; /* regulator id */
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(rsp.data[1], simulated_voltage_mv / 2);
}

ZTEST(msgqueue, test_msg_type_switch_vout_control)
{
	union request req = {0};
	struct response rsp = {0};

	req.data[0] = TT_SMC_MSG_SWITCH_VOUT_CONTROL;
	req.data[1] = 0x01; /* regulator id */
	req.data[2] = 1;    /* enable */
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(i2c_write_buf_emul[0], 1); /*OPERATION command, for readback*/

	zexpect_equal(i2c_write_buf_emul[2], 1);    /*OPERATION command, writ*/
	zexpect_equal(i2c_write_buf_emul[3], 0x12); /* transition_control and command_source high*/
}

ZTEST(msgqueue, test_msg_type_switch_clk_scheme)
{
	union request req = {0};
	struct response rsp = {0};

	/* Reset timer counter and set up the fake */
	timer_counter = 0;

	req.data[0] = TT_SMC_MSG_SWITCH_CLK_SCHEME;
	req.data[1] = TT_CLK_SCHEME_CLOCK_WAVE;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0);
	zassert_equal(clock_wave_value, 2U);

	req.data[1] = TT_CLK_SCHEME_ZERO_SKEW;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0);
	zassert_equal(clock_wave_value, 1U);
}

ZTEST(msgqueue, test_msg_type_debug_noc_translation)
{
	union request req = {0};
	struct response rsp = {0};

	req.data[0] = TT_SMC_MSG_DEBUG_NOC_TRANSLATION | (1U << 8U) /* Enable translation*/
		      | (1U << 9U)                                  /* PCIE Instance  = 1*/
		      | (0U << 10U)                                 /*PCIE instance override*/
		      | (0b1001U << 16U)                            /*Bad tensix columns 0 and 3*/
		;
	req.data[1] = (8U << 0U)/* Bad GDDR 3 */| (0b1010U << 8U) /*skip eth 1 and 3*/;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0);
}

static void test_setup(void *ctx)
{
	(void)ctx;
	ReadReg_fake.custom_fake = ReadReg_msgqueue_fake;
	WriteReg_fake.custom_fake = WriteReg_msgqueue_fake;
	timer_counter = 0U;
	i2c_read_buf_idx = 0U;
	i2c_write_buf_idx = 0U;
	clock_wave_value = 0U;
	memset(i2c_read_buf_emul, 0, sizeof(i2c_read_buf_emul));
	memset(i2c_write_buf_emul, 0, sizeof(i2c_write_buf_emul));
}

ZTEST_SUITE(msgqueue, NULL, NULL, test_setup, NULL, NULL);
