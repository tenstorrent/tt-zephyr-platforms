/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "reg.h"
#include "status_reg.h"
#include "dw_apb_i2c.h"
#include "cm2dm_msg.h"
#include "throttler.h"
#include "asic_state.h"
#include "smbus_target.h"
#include "fan_ctrl.h"

#include <stdint.h>

#include <tenstorrent/post_code.h>
#include <tenstorrent/tt_smbus_regs.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

/* DMFW to CMFW i2c interface is on I2C0 of tensix_sm */
#define CM_I2C_DM_TARGET_INST 0
/* I2C target address for CMFW to respond to DMFW */
#define I2C_TARGET_ADDR       0xA
#define kMaxSmbusMessageSize  64 /* Increase this if larger messages needed */

typedef enum {
	kSmbusStateIdle,
	kSmbusStateCmd,
	kSmbusStateRcvData,
	kSmbusStateRcvPec,
	kSmbusStateSendData,
	kSmbusStateSendPec,
	kSmbusStateWaitIdle, /* After transactions finish, and in error conditions */
} SmbusState;

/* Space to buffer the data */
typedef struct {
	SmbusState state;
	uint8_t command;
	uint8_t blocksize;
	uint8_t rcv_index;
	uint8_t send_index;
	uint8_t received_data[kMaxSmbusMessageSize];
	uint8_t send_data[kMaxSmbusMessageSize];
} SmbusData;

/* Note, all transactions will have PEC */
typedef enum {
	kSmbusTransWriteByte,
	kSmbusTransReadByte,
	kSmbusTransWriteWord,
	kSmbusTransReadWord,
	kSmbusTransBlockWrite,
	kSmbusTransBlockRead,
} SmbusTransType;

/* SMBus receive handler will get the received data passed by reference */
/* Returns 0 on success, any other value on failure */
typedef int32_t (*SmbusRcvHandler)(const uint8_t *data, uint8_t size);
/* SMBus transmit handler will get a pointer to fill in data to send, up to size bytes */
/* Returns 0 on success, any other value on failure */
typedef int32_t (*SmbusSendHandler)(uint8_t *data, uint8_t size);

/* Write commands will have a receive handler, */
/* Read commands will have a send handler */
typedef union {
	SmbusRcvHandler rcv_handler;
	SmbusSendHandler send_handler;
} SmbusHandleData;

typedef struct {

	SmbusTransType trans_type;
	SmbusHandleData handler;
	uint8_t expected_blocksize; /* Only used for block r/w commands */
	uint8_t valid: 1;
	uint8_t pec: 1;
} SmbusCmdDef;

/* Index into cmd_defs array is the command byte */
typedef struct {
	SmbusCmdDef cmd_defs[256];
} SmbusConfig;

/***Start of SMBus handlers***/

static const struct device *const i2c0_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c0));

/* Forward declaration for new command handler */
static int32_t Dm2CmSendFanSpeedHandler(const uint8_t *data, uint8_t size);

int32_t ReadByteTest(uint8_t *data, uint8_t size)
{
	if (size != 1) {
		return -1;
	}

	data[0] = ReadReg(STATUS_FW_SCRATCH_REG_ADDR) & 0xFF;

	return 0;
}

int32_t WriteByteTest(const uint8_t *data, uint8_t size)
{
	if (size != 1) {
		return -1;
	}
	WriteReg(STATUS_FW_SCRATCH_REG_ADDR, size << 16 | data[0]);
	return 0;
}

int32_t ReadWordTest(uint8_t *data, uint8_t size)
{
	if (size != 2) {
		return -1;
	}

	uint32_t tmp = ReadReg(STATUS_FW_SCRATCH_REG_ADDR);

	data[0] = tmp & 0xFF;
	data[1] = (tmp >> 8) & 0xFF;

	return 0;
}

int32_t WriteWordTest(const uint8_t *data, uint8_t size)
{
	if (size != 2) {
		return -1;
	}
	WriteReg(STATUS_FW_SCRATCH_REG_ADDR, size << 16 | data[1] << 8 | data[0]);
	return 0;
}

int32_t BlockReadTest(uint8_t *data, uint8_t size)
{
	if (size != 4) {
		return -1;
	}
	uint32_t tmp = ReadReg(STATUS_FW_SCRATCH_REG_ADDR);

	memcpy(data, &tmp, 4);
	return 0;
}

int32_t BlockWriteTest(const uint8_t *data, uint8_t size)
{
	if (size != 4) {
		return -1;
	}
	uint32_t tmp;

	memcpy(&tmp, data, 4);
	WriteReg(STATUS_FW_SCRATCH_REG_ADDR, tmp);
	return 0;
}

/***End of SMBus handlers***/
static SmbusData smbus_data = {
	.state = kSmbusStateIdle,
};

int32_t UpdateArcStateHandler(const uint8_t *data, uint8_t size)
{
	const uint8_t sig0 = 0xDE;
	const uint8_t sig1 = 0xAF;

	if (size != 3U || data[1] != sig0 || data[2] != sig1) {
		return -1;
	}

	set_asic_state(data[0]);
	return 0;
}

/* clang-format off */
static SmbusConfig smbus_config = {
	.cmd_defs = {
		[CMFW_SMBUS_REQ] = {.valid = 1,
				    .pec = 1U,
				    .trans_type = kSmbusTransBlockRead,
				    .expected_blocksize = 6,
				    .handler = {.send_handler = &Cm2DmMsgReqSmbusHandler}},
		[CMFW_SMBUS_ACK] = {.valid = 1,
				    .pec = 1U,
				    .trans_type = kSmbusTransWriteWord,
				    .handler = {.rcv_handler = &Cm2DmMsgAckSmbusHandler}},
		[CMFW_SMBUS_UPDATE_ARC_STATE] = {.valid = 1,
						 .pec = 0U,
						 .trans_type = kSmbusTransBlockWrite,
						 .expected_blocksize = 3,
						 .handler = {.rcv_handler =
								     &UpdateArcStateHandler}},
		[CMFW_SMBUS_DM_STATIC_INFO] = {.valid = 1,
					       .pec = 1U,
					       .trans_type = kSmbusTransBlockWrite,
					       .expected_blocksize = sizeof(dmStaticInfo),
					       .handler = {.rcv_handler = &Dm2CmSendDataHandler}},
		[CMFW_SMBUS_PING] = {.valid = 1,
				     .pec = 1U,
				     .trans_type = kSmbusTransWriteWord,
				     .handler = {.rcv_handler = &Dm2CmPingHandler}},
		[CMFW_SMBUS_FAN_SPEED] = {.valid = 1,
					.pec = 1U,
					.trans_type = kSmbusTransWriteWord,
					.handler = {.rcv_handler = &Dm2CmSendFanSpeedHandler}},
		[CMFW_SMBUS_FAN_RPM] = {.valid = 1,
					.pec = 1U,
					.trans_type = kSmbusTransWriteWord,
					.handler = {.rcv_handler = &Dm2CmSendFanRPMHandler}},
#ifndef CONFIG_TT_SMC_RECOVERY
		[CMFW_SMBUS_POWER_LIMIT] = {.valid = 1,
					    .pec = 1U,
					    .trans_type = kSmbusTransWriteWord,
					    .handler = {.rcv_handler = &Dm2CmSetBoardPowerLimit}},
		[CMFW_SMBUS_POWER_INSTANT] = {.valid = 1,
					      .pec = 1U,
					      .trans_type = kSmbusTransWriteWord,
					      .handler = {.rcv_handler = &Dm2CmSendPowerHandler}},
		[0x26] = {.valid = 1,
			  .pec = 1U,
			  .trans_type = kSmbusTransWriteByte,
			  .handler = {.rcv_handler = &SMBusTelemRegHandler}},
		[0x27] = {.valid = 1,
			  .pec = 1U,
			  .trans_type = kSmbusTransBlockRead,
			  .expected_blocksize = sizeof(uint32_t),
			  .handler = {.send_handler = &SMBusTelemDataHandler}},
		[CMFW_SMBUS_THERM_TRIP_COUNT] = {
				.valid = 1,
				.pec = 1U,
				.trans_type = kSmbusTransWriteWord,
				.handler = {.rcv_handler = &Dm2CmSendThermTripCountHandler}},
#endif
		[CMFW_SMBUS_TEST_READ] = {.valid = 1,
					  .pec = 1U,
					  .trans_type = kSmbusTransReadByte,
					  .handler = {.send_handler = &ReadByteTest}},
		[CMFW_SMBUS_TEST_WRITE] = {.valid = 1,
					   .pec = 1U,
					   .trans_type = kSmbusTransWriteByte,
					   .handler = {.rcv_handler = &WriteByteTest}},
		[CMFW_SMBUS_TEST_READ_WORD] = {.valid = 1,
			 .pec =
				 1U,
			 .trans_type = kSmbusTransReadWord,
			 .handler = {.send_handler = &ReadWordTest}},
		[CMFW_SMBUS_TEST_WRITE_WORD] = {.valid = 1,
						.pec = 1U,
						.trans_type = kSmbusTransWriteWord,
						.handler = {.rcv_handler = &WriteWordTest}},
		[CMFW_SMBUS_TEST_READ_BLOCK] = {.valid = 1,
						.pec = 1U,
						.trans_type = kSmbusTransBlockRead,
						.expected_blocksize = 4,
						.handler = {.send_handler = &BlockReadTest}},
		[CMFW_SMBUS_TEST_WRITE_BLOCK] = {.valid = 1,
						 .pec = 1U,
						 .trans_type = kSmbusTransBlockWrite,
						 .expected_blocksize = 4,
						 .handler = {.rcv_handler = &BlockWriteTest}},
	}};
/* clang-format on */

static SmbusCmdDef *GetCmdDef(uint8_t cmd)
{
	return &smbus_config.cmd_defs[cmd];
}

static uint8_t Crc8(uint8_t crc, uint8_t data)
{
	return crc8(&data, 1, 0x7, crc /* pec */, false);
}

static int32_t Dm2CmSendFanSpeedHandler(const uint8_t *data, uint8_t size)
{
#ifndef CONFIG_TT_SMC_RECOVERY
	if (size != 2) {
		return -1;
	}

	uint16_t speed = sys_get_le16(data);
	/* 0 => unforce */
	FanCtrlApplyBoardForcedSpeed(speed);

	return 0;
#endif

	return -1;
}

static int I2CWriteHandler(struct i2c_target_config *config, uint8_t val)
{
	SmbusCmdDef *curr_cmd = GetCmdDef(smbus_data.command);

	if (smbus_data.state == kSmbusStateIdle) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1030);
		smbus_data.command = val;
		curr_cmd = GetCmdDef(smbus_data.command);
		if (!curr_cmd->valid) {
			/* Command not implemented */
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
		smbus_data.state = kSmbusStateCmd;
	} else if (smbus_data.state == kSmbusStateCmd) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1040);
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockWrite:
			smbus_data.blocksize = val;
			if (smbus_data.blocksize != curr_cmd->expected_blocksize) {
				smbus_data.state = kSmbusStateWaitIdle;
				return -1;
			}
			smbus_data.state = kSmbusStateRcvData;
			break;
		case kSmbusTransWriteByte:
			smbus_data.blocksize = 1;
			smbus_data.received_data[smbus_data.rcv_index++] = val;

			if (1U == curr_cmd->pec) {
				smbus_data.state = kSmbusStateRcvPec;
			} else {
				int32_t ret = curr_cmd->handler.rcv_handler(
					smbus_data.received_data, smbus_data.blocksize);
				smbus_data.state = kSmbusStateWaitIdle;
				return ret;
			}
			break;
		case kSmbusTransWriteWord:
			smbus_data.blocksize = 2;
			smbus_data.received_data[smbus_data.rcv_index++] = val;
			smbus_data.state = kSmbusStateRcvData;
			break;
		default:
			/* Error, invalid command for write */
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
	} else if (smbus_data.state == kSmbusStateRcvData) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1050);
		smbus_data.received_data[smbus_data.rcv_index++] = val;
		if (smbus_data.rcv_index == smbus_data.blocksize) {
			if (1U == curr_cmd->pec) {
				smbus_data.state = kSmbusStateRcvPec;
			} else {
				int32_t ret = curr_cmd->handler.rcv_handler(
					smbus_data.received_data, smbus_data.blocksize);
				smbus_data.state = kSmbusStateWaitIdle;
				return ret;
			}
		}
	} else if (smbus_data.state == kSmbusStateRcvPec) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1060);
		uint8_t rcv_pec = val;

		/* Calculate the PEC */
		uint8_t pec = 0;

		pec = Crc8(pec, I2C_TARGET_ADDR << 1 | I2C_WRITE_BIT); /* start address byte */
		pec = Crc8(pec, smbus_data.command);
		if (curr_cmd->trans_type == kSmbusTransBlockWrite) {
			pec = Crc8(pec, smbus_data.blocksize);
		}
		for (int i = 0; i < smbus_data.blocksize; i++) {
			pec = Crc8(pec, smbus_data.received_data[i]);
		}

		if (pec != rcv_pec) {
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
		int32_t ret = curr_cmd->handler.rcv_handler(smbus_data.received_data,
							    smbus_data.blocksize);
		smbus_data.state = kSmbusStateWaitIdle;
		return ret;
	} else {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
			 0xc2de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR));
		smbus_data.state = kSmbusStateWaitIdle;
		return -1;
	}
	return 0;
}

static int I2CReadHandler(struct i2c_target_config *config, uint8_t *val)
{
	SmbusCmdDef *curr_cmd = GetCmdDef(smbus_data.command);

	if (smbus_data.state == kSmbusStateCmd) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0010);
		/* Calculate blocksize for different types of commands */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockRead:
			smbus_data.blocksize = curr_cmd->expected_blocksize;
			break;
		case kSmbusTransReadByte:
			smbus_data.blocksize = 1;
			break;
		case kSmbusTransReadWord:
			smbus_data.blocksize = 2;
			break;
		default:
			/* Error, invalid command for read */
			smbus_data.state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
		/* Call the send handler to get the data */
		if (curr_cmd->handler.send_handler(smbus_data.send_data, smbus_data.blocksize)) {
			WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0020);
			/* Send handler returned error */
			smbus_data.state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
		/* Send the correct data for different types of commands */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockRead:
			WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0030);
			*val = smbus_data.blocksize;
			smbus_data.state = kSmbusStateSendData;
			break;
		case kSmbusTransReadByte:
			*val = smbus_data.send_data[smbus_data.send_index++];
			smbus_data.state = curr_cmd->pec ? kSmbusStateSendPec : kSmbusStateWaitIdle;
			break;
		case kSmbusTransReadWord:
			*val = smbus_data.send_data[smbus_data.send_index++];
			smbus_data.state = kSmbusStateSendData;
			break;
		default:
			WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0040);
			/* Error, invalid command for read */
			smbus_data.state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
	} else if (smbus_data.state == kSmbusStateSendData) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0050);
		*val = smbus_data.send_data[smbus_data.send_index++];
		if (smbus_data.send_index == smbus_data.blocksize) {
			smbus_data.state = curr_cmd->pec ? kSmbusStateSendPec : kSmbusStateWaitIdle;
		}
	} else if (smbus_data.state == kSmbusStateSendPec) {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0060);
		/* Calculate and send PEC. This is a read-type operation, so it starts with a
		 * sequence of writes, then some reads.
		 */

		uint8_t pec = 0;

		pec = Crc8(pec, I2C_TARGET_ADDR << 1 | I2C_WRITE_BIT); /* start address byte */
		pec = Crc8(pec, smbus_data.command);

		/* any received data */
		for (int i = 0; i < smbus_data.rcv_index; i++) {
			pec = Crc8(pec, smbus_data.received_data[i]);
		}

		pec = Crc8(pec, I2C_TARGET_ADDR << 1 | I2C_READ_BIT); /* restart address byte */

		/* sent data */
		if (curr_cmd->trans_type == kSmbusTransBlockRead) {
			pec = Crc8(pec, smbus_data.blocksize);
		}
		for (int i = 0; i < smbus_data.blocksize; i++) {
			pec = Crc8(pec, smbus_data.send_data[i]);
		}

		*val = pec;
		smbus_data.state = kSmbusStateWaitIdle;
	} else {
		WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
			 0xc1de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR));
		smbus_data.state = kSmbusStateWaitIdle;
		*val = 0xFF;
		return -1;
	}
	return 0;
}

static int I2CStopHandler(struct i2c_target_config *config)
{
	smbus_data.state = kSmbusStateIdle;
	smbus_data.command = 0;
	smbus_data.blocksize = 0;
	smbus_data.rcv_index = 0;
	smbus_data.send_index = 0;
	WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
		 0xc3de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR));
	/* Don't erase data buffers for efficiency */
	return 0;
}
#if CONFIG_BOARD_NATIVE_SIM
static int I2CWriteRequested(struct i2c_target_config *config)
{
	(void)config;
	return 0;
}
#endif

const struct i2c_target_callbacks i2c_target_cb_impl = {
	.write_received = &I2CWriteHandler,
	.read_requested = &I2CReadHandler,
#if CONFIG_BOARD_NATIVE_SIM
	.write_requested = &I2CWriteRequested,
	.read_processed = &I2CReadHandler,
#endif
	.stop = &I2CStopHandler,
};

struct i2c_target_config i2c_target_config_impl = {
	.address = I2C_TARGET_ADDR,
	.callbacks = &i2c_target_cb_impl,
};

static int InitSmbusTarget(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPB);

	if (IS_ENABLED(CONFIG_ARC)) {
		I2CInitGPIO(CM_I2C_DM_TARGET_INST);
	}

	i2c_target_register(i2c0_dev, &i2c_target_config_impl);
	return 0;
}
SYS_INIT(InitSmbusTarget, APPLICATION, 16);

void PollSmbusTarget(void)
{
	PollI2CSlave(CM_I2C_DM_TARGET_INST);
	WriteReg(I2C0_TARGET_DEBUG_STATE_2_REG_ADDR, 0xfaca);
}
