/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/ztest.h>

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include "asic_state.h"
#include "clock_wave.h"
#include "cm2dm_msg.h"
#include "noc_init.h"

#include "reg_mock.h"

/* Custom fake for ReadReg to simulate timer progression */
#define RESET_UNIT_REFCLK_CNT_LO_REG_ADDR         0x800300E0
#define PLL_CNTL_WRAPPER_CLOCK_WAVE_CNTL_REG_ADDR 0x80020038
static uint32_t timer_counter;
static uint8_t i2c_read_buf_emul[256] = {0};
static uint8_t i2c_read_buf_idx;

static uint8_t i2c_write_buf_emul[256] = {0};
static uint8_t i2c_write_buf_idx;

static uint32_t clock_wave_value;
static uint32_t noc_2_axi_last_write;

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

	/*BH_PCIE_DWC_PCIE_USP_PF0_MSI_CAP_PCI_MSI_CAP_ID_NEXT_CTRL_REG_REG_ADDR*/
	if (addr == 0xCE000050) {
		return BIT(16) | BIT(20); /*pci_msi_enable | pci_msi_multiple_msg_en == 1*/
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

	if (addr == 0xC0000000) {
		noc_2_axi_last_write = value;
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
	const struct device *pll4 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pll4));
	union request req = {0};
	struct response rsp = {0};

	/* LSB to MSB:
	 * 0x21: TT_SMC_MSG_POWER_SETTING
	 * 0x04: 4 power flags valid, 0 power settings valid
	 * 0x0003: max_ai_clk on, mrisc power on, tensix power off, l2cpu off
	 */
	req.data[0] = 0x00030421;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0x0);

	/* Validate that status of emulated L2CPUCLKs are disabled */
	zassert_true(device_is_ready(pll4));
	zassert_equal(clock_control_get_status(
			      pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_0),
		      CLOCK_CONTROL_STATUS_OFF);
	zassert_equal(clock_control_get_status(
			      pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_1),
		      CLOCK_CONTROL_STATUS_OFF);
	zassert_equal(clock_control_get_status(
			      pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_2),
		      CLOCK_CONTROL_STATUS_OFF);
	zassert_equal(clock_control_get_status(
			      pll4, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_L2CPUCLK_3),
		      CLOCK_CONTROL_STATUS_OFF);
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

	req.data[0] = TT_SMC_MSG_DEBUG_NOC_TRANSLATION | (BIT(0) << 8U) /* Enable translation*/
		      | (BIT(1) << 8U)                                  /* PCIE Instance  = 1*/
		      | (BIT(2) << 8U)                                  /*PCIE instance override*/
		      | ((BIT(0) | BIT(3)) << 16U) /*Bad tensix columns 0 and 3*/
		;
	req.data[1] = 8U /* Bad GDDR 8 */ | ((BIT(1) | BIT(3)) << 8U) /*skip eth 1 and 3*/;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 234); /* uin8_t EINVAL -> GDDR out of range*/

	req.data[1] = NO_BAD_GDDR | ((BIT(1) | BIT(3)) << 8U) /*skip eth 1 and 3*/;

	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0); /*OK*/
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

ZTEST(msgqueue, test_msg_type_send_pcie_msi)
{
	union request req = {0};
	struct response rsp = {0};

	noc_2_axi_last_write = 0xffffffffU;
	req.data[0] = TT_SMC_MSG_SEND_PCIE_MSI | (BIT(0) << 8U) /*PCIe instance 1*/;
	req.data[1] = 0x00; /* MSI number */
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(noc_2_axi_last_write, 0);

	req.data[1] = 0x01; /* MSI number */

	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(noc_2_axi_last_write, 1);
}

ZTEST(msgqueue, test_msg_type_i2c_message_bad_line_id)
{
	union request req = {0};
	struct response rsp = {0};

	/* Reset timer counter and set up the fake */
	timer_counter = 0;
	ReadReg_fake.custom_fake = ReadReg_msgqueue_fake;

	req.data[0] = BIT(24U)                     /*Write Operation*/
		      | FIELD_PREP(0xFF0000, 0x50) /* target address */
		      | FIELD_PREP(0xFF00, 0x5U)   /*Invalid Line Id*/
		      | TT_SMC_MSG_I2C_MESSAGE;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 1);
}

ZTEST(msgqueue, test_msg_type_i2c_message)
{
	union request req = {0};
	struct response rsp = {0};

	i2c_read_buf_emul[0] = 0x1U;
	i2c_read_buf_emul[1] = 0x2U;
	i2c_read_buf_emul[2] = 0x3U;
	i2c_read_buf_emul[3] = 0x4U;

	/* Reset timer counter and set up the fake */
	timer_counter = 0;
	ReadReg_fake.custom_fake = ReadReg_msgqueue_fake;

	req.data[0] = FIELD_PREP(0xFF000000, 0x4)  /*Write 4 bytes*/
		      | FIELD_PREP(0xFF0000, 0x50) /* target address */
		      | FIELD_PREP(0xFF00, 0x1U)   /*Line Id*/
		      | TT_SMC_MSG_I2C_MESSAGE;

	req.data[1] = 4Ul;         /*Read 4 bytes*/
	req.data[2] = 0xDDCCBBAAU; /*Write data*/
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(rsp.data[1], 0x04030201);

	zexpect_equal(i2c_write_buf_emul[0], 0xaa);
	zexpect_equal(i2c_write_buf_emul[1], 0xbb);
	zexpect_equal(i2c_write_buf_emul[2], 0xcc);
	zexpect_equal(i2c_write_buf_emul[3], 0xdd);
}

ZTEST(msgqueue, test_msg_type_blink_led)
{
	union request req = {0};
	struct response rsp = {0};
	uint8_t request_data[6];
	uint8_t request_size = sizeof(request_data);
	cm2dmMessage *msg;
	int ret;

	req.data[0] = TT_SMC_MSG_BLINKY;
	req.data[1] = 0x1;

	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[0], 0);
	zassert_equal(rsp.data[1], 0);

	PostCm2DmMsg(kCm2DmMsgIdLedBlink, 1);

	ret = Cm2DmMsgReqSmbusHandler(request_data, &request_size);
	zassert_equal(ret, 0);
	zassert_equal(request_size, 6);

	msg = (cm2dmMessage *)request_data;
	zassert_equal(msg->msg_id, kCm2DmMsgIdLedBlink);
	zassert_equal(msg->data, 1);
}

ZTEST(msgqueue, test_msg_type_test)
{
	union request req = {0};
	struct response rsp = {0};

	req.data[0] = TT_SMC_MSG_TEST;
	req.data[1] = 42; /* test_value to be incremented */
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(rsp.data[1], 43); /* test_value + 1 */
}

ZTEST(msgqueue, test_msg_type_asic_state)
{
	union request req = {0};
	struct response rsp = {0};

	req.data[0] = TT_SMC_MSG_ASIC_STATE3;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(get_asic_state(), A3State);

	/* Test ASIC_STATE0 to return to state 0 */
	req.data[0] = TT_SMC_MSG_ASIC_STATE0;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zexpect_equal(rsp.data[0], 0);
	zexpect_equal(get_asic_state(), A0State);
}

ZTEST_SUITE(msgqueue, NULL, NULL, test_setup, NULL, NULL);
