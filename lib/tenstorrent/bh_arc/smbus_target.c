/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include "reg.h"
#include "status_reg.h"
#include "dw_apb_i2c.h"
#include "cm2bm_msg.h"
#include <stdint.h>

/* BMFW to CMFW i2c interface is on I2C0 of tensix_sm */
#define CM_I2C_BM_TARGET_INST 0
/* I2C target address for CMFW to respond to BMFW */
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
	uint8_t valid;
	SmbusTransType trans_type;
	uint8_t expected_blocksize; /* Only used for block r/w commands */
	SmbusHandleData handler;
} SmbusCmdDef;

/* Index into cmd_defs array is the command byte */
typedef struct {
	SmbusCmdDef cmd_defs[256];
} SmbusConfig;

/***Start of SMBus handlers***/

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
static SmbusConfig smbus_config = {
	.cmd_defs = {
		[0x10] = {.valid = 1,
			  .trans_type = kSmbusTransBlockRead,
			  .expected_blocksize = 6,
			  .handler = {

					  .send_handler = &Cm2BmMsgReqSmbusHandler,
				  }},
		[0x11] = {.valid = 1,
			  .trans_type = kSmbusTransWriteWord,
			  .handler = {

					  .rcv_handler = &Cm2BmMsgAckSmbusHandler,
				  }},
		[0x20] = {.valid = 1,
			  .trans_type = kSmbusTransBlockWrite,
			  .expected_blocksize = sizeof(bmStaticInfo),
			  .handler = {

					  .rcv_handler = &Bm2CmSendDataHandler,
				  }},
		[0x21] = {.valid = 1,
			  .trans_type = kSmbusTransWriteWord,
			  .handler = {

					  .rcv_handler = &Bm2CmPingHandler,
				  }},
		[0xD8] = {

				.valid = 1,
				.trans_type = kSmbusTransReadByte,
				.handler = {

						.send_handler = &ReadByteTest,
					},
			},
		[0xD9] = {

				.valid = 1,
				.trans_type = kSmbusTransWriteByte,
				.handler = {

						.rcv_handler = &WriteByteTest,
					},
			},
		[0xDA] = {

				.valid = 1,
				.trans_type = kSmbusTransReadWord,
				.handler = {

						.send_handler = &ReadWordTest,
					},
			},
		[0xDB] = {

				.valid = 1,
				.trans_type = kSmbusTransWriteWord,
				.handler = {

						.rcv_handler = &WriteWordTest,
					},
			},
		[0xDC] = {

				.valid = 1,
				.trans_type = kSmbusTransBlockRead,
				.expected_blocksize = 4,
				.handler = {

						.send_handler = &BlockReadTest,
					},
			},
		[0xDD] = {

				.valid = 1,
				.trans_type = kSmbusTransBlockWrite,
				.expected_blocksize = 4,
				.handler = {

						.rcv_handler = &BlockWriteTest,
					},
			},
	}};

static SmbusCmdDef *GetCmdDef(uint8_t cmd)
{
	return &smbus_config.cmd_defs[cmd];
}

static uint8_t Crc8(uint8_t crc, uint8_t data)
{
	uint8_t i;

	crc ^= data;
	for (i = 0; i < 8; i++) {
		if (crc & 0x80) {
			crc = (crc << 1) ^ 0x07;
		} else {
			crc <<= 1;
		}
	}
	return crc;
}

static int I2CWriteHandler(struct i2c_target_config *config, uint8_t val)
{
	SmbusCmdDef *curr_cmd = GetCmdDef(smbus_data.command);

	if (smbus_data.state == kSmbusStateIdle) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de1030);
		smbus_data.command = val;
		curr_cmd = GetCmdDef(smbus_data.command);
		if (!curr_cmd->valid) {
			/* Command not implemented */
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
		smbus_data.state = kSmbusStateCmd;
	} else if (smbus_data.state == kSmbusStateCmd) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de1040);
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
			smbus_data.state = kSmbusStateRcvPec;
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
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de1050);
		smbus_data.received_data[smbus_data.rcv_index++] = val;
		if (smbus_data.rcv_index == smbus_data.blocksize) {
			smbus_data.state = kSmbusStateRcvPec;
		}
	} else if (smbus_data.state == kSmbusStateRcvPec) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de1060);
		uint8_t rcv_pec = val;

		/* Calculate the PEC */
		uint8_t pec = 0;

		pec = Crc8(pec, I2C_TARGET_ADDR << 1 |
					I2C_WRITE_BIT); /* Address byte needs to be included */
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
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19),
			 0xc2de0000 | ReadReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19)));
		smbus_data.state = kSmbusStateWaitIdle;
		return -1;
	}
	return 0;
}

static int I2CReadHandler(struct i2c_target_config *config, uint8_t *val)
{
	SmbusCmdDef *curr_cmd = GetCmdDef(smbus_data.command);

	if (smbus_data.state == kSmbusStateCmd) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0010);
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
			return -1;
		}
		/* Call the send handler to get the data */
		if (curr_cmd->handler.send_handler(smbus_data.send_data, smbus_data.blocksize)) {
			WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0020);
			/* Send handler returned error */
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
		/* Send the correct data for different types of commands */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockRead:
			WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0030);
			*val = smbus_data.blocksize;
			smbus_data.state = kSmbusStateSendData;
			break;
		case kSmbusTransReadByte:
			*val = smbus_data.send_data[smbus_data.send_index++];
			smbus_data.state = kSmbusStateSendPec;
			break;
		case kSmbusTransReadWord:
			*val = smbus_data.send_data[smbus_data.send_index++];
			smbus_data.state = kSmbusStateSendData;
			break;
		default:
			WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0040);
			/* Error, invalid command for read */
			smbus_data.state = kSmbusStateWaitIdle;
			return -1;
		}
	} else if (smbus_data.state == kSmbusStateSendData) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0050);
		*val = smbus_data.send_data[smbus_data.send_index++];
		if (smbus_data.send_index == smbus_data.blocksize) {
			smbus_data.state = kSmbusStateSendPec;
		}
	} else if (smbus_data.state == kSmbusStateSendPec) {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19), 0xc0de0060);
		/* Calculate PEC then send it */
		uint8_t pec = 0;

		pec = Crc8(pec, I2C_TARGET_ADDR << 1 |
					I2C_READ_BIT); /* Address byte needs to be included */
		pec = Crc8(pec, smbus_data.command);
		if (curr_cmd->trans_type == kSmbusTransBlockRead) {
			pec = Crc8(pec, smbus_data.blocksize);
		}
		for (int i = 0; i < smbus_data.blocksize; i++) {
			pec = Crc8(pec, smbus_data.send_data[i]);
		}
		*val = pec;
		smbus_data.state = kSmbusStateWaitIdle;
	} else {
		WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19),
			 0xc1de0000 | ReadReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19)));
		smbus_data.state = kSmbusStateWaitIdle;
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
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19),
		 0xc3de0000 | ReadReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(19)));
	/* Don't erase data buffers for efficiency */
	return 0;
}

const struct i2c_target_callbacks i2c_target_cb_impl = {
	.write_received = &I2CWriteHandler,
	.read_processed = &I2CReadHandler,
	.stop = &I2CStopHandler,
};

void InitSmbusTarget(void)
{
	I2CInit(I2CSlv, I2C_TARGET_ADDR, I2CFastMode, CM_I2C_BM_TARGET_INST);
	SetI2CSlaveCallbacks(CM_I2C_BM_TARGET_INST, &i2c_target_cb_impl);
}

void PollSmbusTarget(void)
{
	PollI2CSlave(CM_I2C_BM_TARGET_INST);
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(20), 0xfaca);
}
