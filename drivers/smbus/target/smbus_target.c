/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_smbus_target

#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/crc.h>
#include <tenstorrent/smbus_target.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2c_target);

typedef enum {
	kSmbusStateIdle,
	kSmbusStateCmd,
	kSmbusStateRcvData,
	kSmbusStateRcvPec,
	kSmbusStateSendData,
	kSmbusStateSendPec,
	kSmbusStateWaitIdle, /* After transactions finish, and in error conditions */
} SmbusState;

struct smbus_target_data {
	struct i2c_target_config config;
	/* clang-format off */
	const SmbusCmdDef * cmd_defs[256];
	/* clang-format on */
	SmbusState state;
	uint8_t command;
	uint8_t blocksize;
	uint8_t rcv_index;
	uint8_t send_index;
	uint8_t received_data[CONFIG_SMBUS_MAX_MSG_SIZE];
	uint8_t send_data[CONFIG_SMBUS_MAX_MSG_SIZE];
};

struct smbus_target_config {
	struct i2c_dt_spec bus;
};

static const SmbusCmdDef *get_cmd_def(struct smbus_target_data *smbus_data, uint8_t cmd)
{
	return smbus_data->cmd_defs[cmd];
}

static inline uint8_t pec_crc_8(uint8_t crc, uint8_t data)
{
	return crc8(&data, 1, 0x7, crc, false);
}

static int32_t smbus_target_register(const struct device *dev)
{
	const struct smbus_target_config *cfg = dev->config;
	struct smbus_target_data *data = dev->data;

	return i2c_target_register(cfg->bus.bus, &data->config);
}

static int32_t smbus_target_unregister(const struct device *dev)
{
	const struct smbus_target_config *cfg = dev->config;
	struct smbus_target_data *data = dev->data;

	return i2c_target_unregister(cfg->bus.bus, &data->config);
}

static int smbus_write_handler(struct i2c_target_config *config, uint8_t val)
{
	int32_t ret = 0;
	struct smbus_target_data *smbus_data =
		CONTAINER_OF(config, struct smbus_target_data, config);
	const SmbusCmdDef *curr_cmd = get_cmd_def(smbus_data, smbus_data->command);

	if (smbus_data->state == kSmbusStateIdle) {
		/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1030); */
		smbus_data->command = val;
		curr_cmd = get_cmd_def(smbus_data, smbus_data->command);
		if (!curr_cmd) {
			/* Command not implemented */
			smbus_data->state = kSmbusStateWaitIdle;
			return -1;
		}
		smbus_data->state = kSmbusStateCmd;
	} else if (smbus_data->state == kSmbusStateCmd) {
		/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1040); */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockWrite:
		case kSmbusTransBlockWriteBlockRead:
			smbus_data->blocksize = val;
			if ((!curr_cmd->variable_blocksize) &&
			    smbus_data->blocksize != curr_cmd->expected_blocksize_w) {
				smbus_data->state = kSmbusStateWaitIdle;
				return -1;
			}
			smbus_data->state = kSmbusStateRcvData;
			break;
		case kSmbusTransWriteByte:
			smbus_data->blocksize = 1;
			smbus_data->received_data[smbus_data->rcv_index++] = val;

			if (1U == curr_cmd->pec) {
				smbus_data->state = kSmbusStateRcvPec;
			} else {
				ret = curr_cmd->rcv_handler(smbus_data->received_data,
							    smbus_data->blocksize);

				smbus_data->state = kSmbusStateWaitIdle;
			}
			break;
		case kSmbusTransWriteWord:
			smbus_data->blocksize = 2;
			smbus_data->received_data[smbus_data->rcv_index++] = val;
			smbus_data->state = kSmbusStateRcvData;
			break;
		default:
			/* Error, invalid command for write */
			smbus_data->state = kSmbusStateWaitIdle;
			ret = -1;
		}
	} else if (smbus_data->state == kSmbusStateRcvData) {
		/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1050); */
		smbus_data->received_data[smbus_data->rcv_index++] = val;
		if (smbus_data->rcv_index == smbus_data->blocksize) {
			if (1U == curr_cmd->pec &&
			    (curr_cmd->trans_type != kSmbusTransBlockWriteBlockRead)) {
				smbus_data->state = kSmbusStateRcvPec;
			} else {
				ret = curr_cmd->rcv_handler(smbus_data->received_data,
							    smbus_data->blocksize);
				if (ret == 0 &&
				    curr_cmd->trans_type == kSmbusTransBlockWriteBlockRead) {
					smbus_data->state = kSmbusStateCmd;
				} else {
					smbus_data->state = kSmbusStateWaitIdle;
				}
			}
		}
	} else if (smbus_data->state == kSmbusStateRcvPec) {
		/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de1060);*/
		uint8_t rcv_pec = val;

		/* Calculate the PEC */
		uint8_t pec = 0;

		pec = pec_crc_8(pec, smbus_data->config.address << 1 |
					     I2C_MSG_WRITE); /* start address byte */
		pec = pec_crc_8(pec, smbus_data->command);
		if (curr_cmd->trans_type == kSmbusTransBlockWrite) {
			pec = pec_crc_8(pec, smbus_data->blocksize);
		}
		for (int i = 0; i < smbus_data->blocksize; i++) {
			pec = pec_crc_8(pec, smbus_data->received_data[i]);
		}

		if (pec != rcv_pec) {
			smbus_data->blocksize = kSmbusStateWaitIdle;
			return -1;
		}
		ret = curr_cmd->rcv_handler(smbus_data->received_data, smbus_data->blocksize);

		if (ret == 0 && curr_cmd->trans_type == kSmbusTransBlockWriteBlockRead) {
			smbus_data->state = kSmbusStateCmd;
		} else {
			smbus_data->state = kSmbusStateWaitIdle;
		}
	} else {
		/* Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
		 * 0xc2de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR));
		 */
		smbus_data->state = kSmbusStateWaitIdle;
		ret = -1;
	}
	return ret;
}

static int32_t smbus_read_handler(struct i2c_target_config *config, uint8_t *val)
{
	struct smbus_target_data *smbus_data =
		CONTAINER_OF(config, struct smbus_target_data, config);

	const SmbusCmdDef *curr_cmd = get_cmd_def(smbus_data, smbus_data->command);

	if (smbus_data->state == kSmbusStateCmd) {
		/* Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0010); */

		/* Calculate blocksize for different types of commands */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockRead:
		case kSmbusTransBlockWriteBlockRead:
			smbus_data->blocksize = curr_cmd->expected_blocksize_r;
			break;
		case kSmbusTransReadByte:
			smbus_data->blocksize = 1;
			break;
		case kSmbusTransReadWord:
			smbus_data->blocksize = 2;
			break;
		default:
			/* Error, invalid command for read */
			smbus_data->state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
		/* Call the send handler to get the data */
		if (curr_cmd->send_handler(smbus_data->send_data, smbus_data->blocksize)) {
			/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
			 * 0xc0de0020);
			 */
			/* Send handler returned error */
			smbus_data->state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
		/* Send the correct data for different types of commands */
		switch (curr_cmd->trans_type) {
		case kSmbusTransBlockWriteBlockRead:
		case kSmbusTransBlockRead:
			/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
			 * 0xc0de0030);
			 */
			*val = smbus_data->blocksize;
			smbus_data->state = kSmbusStateSendData;
			break;
		case kSmbusTransReadByte:
			*val = smbus_data->send_data[smbus_data->send_index++];
			smbus_data->state =
				curr_cmd->pec ? kSmbusStateSendPec : kSmbusStateWaitIdle;
			break;
		case kSmbusTransReadWord:
			*val = smbus_data->send_data[smbus_data->send_index++];
			smbus_data->state = kSmbusStateSendData;
			break;
		default:
			/* Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
			 * 0xc0de0040);
			 */
			/* Error, invalid command for read */
			smbus_data->state = kSmbusStateWaitIdle;
			*val = 0xFF;
			return -1;
		}
	} else if (smbus_data->state == kSmbusStateSendData) {
		/* Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0050); */
		*val = smbus_data->send_data[smbus_data->send_index++];
		if (smbus_data->send_index == smbus_data->blocksize) {
			smbus_data->state =
				curr_cmd->pec ? kSmbusStateSendPec : kSmbusStateWaitIdle;
		}
	} else if (smbus_data->state == kSmbusStateSendPec) {
		/* Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, 0xc0de0060);*/
		/* Calculate and send PEC. This is a read-type operation, so it starts with a
		 * sequence of writes, then some reads.
		 */

		uint8_t pec = 0;

		pec = pec_crc_8(pec, smbus_data->config.address << 1 |
					     I2C_MSG_WRITE); /* start address byte */
		pec = pec_crc_8(pec, smbus_data->command);

		if (curr_cmd->trans_type == kSmbusTransBlockWriteBlockRead) {
			pec = pec_crc_8(pec, curr_cmd->expected_blocksize_w);
		}
		/* any received data */
		for (int i = 0; i < smbus_data->rcv_index; i++) {
			pec = pec_crc_8(pec, smbus_data->received_data[i]);
		}

		pec = pec_crc_8(pec, smbus_data->config.address << 1 |
					     I2C_MSG_READ); /* restart address byte */

		/* sent data */
		if (curr_cmd->trans_type == kSmbusTransBlockRead ||
		    curr_cmd->trans_type == kSmbusTransBlockWriteBlockRead) {
			pec = pec_crc_8(pec, smbus_data->blocksize);
		}
		for (int i = 0; i < smbus_data->blocksize; i++) {
			pec = pec_crc_8(pec, smbus_data->send_data[i]);
		}

		*val = pec;
		smbus_data->state = kSmbusStateWaitIdle;
	} else {
		/*Log something like WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR,
		 *	 0xc1de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR));
		 */
		smbus_data->state = kSmbusStateWaitIdle;
		*val = 0xFF;
		return -1;
	}
	return 0;
}

static int32_t smbus_stop_handler(struct i2c_target_config *config)
{
	struct smbus_target_data *smbus_data =
		CONTAINER_OF(config, struct smbus_target_data, config);

	smbus_data->state = kSmbusStateIdle;
	smbus_data->command = 0;
	smbus_data->blocksize = 0;
	smbus_data->rcv_index = 0;
	smbus_data->send_index = 0;

	/* Log something like */
	/* WriteReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR, */
	/* 0xc3de0000 | ReadReg(I2C0_TARGET_DEBUG_STATE_REG_ADDR)); */

	/* Don't erase data buffers for efficiency */
	return 0;
}

/*Unfortunately, there is a delta between how I2C target controllers treat
 *Incoming data streams. The i2c_emul controller requires this CB be implemented,
 *otherwise it will trip on a NULL ptr dereference.
 */
static int32_t smbus_write_requested(struct i2c_target_config *config)
{
	(void)config;
	return 0;
}

static const struct i2c_target_driver_api api_funcs = {
	.driver_register = smbus_target_register,
	.driver_unregister = smbus_target_unregister,
};

const struct i2c_target_callbacks smbus_target_cb_impl = {
	.write_requested = &smbus_write_requested,
	.write_received = &smbus_write_handler,
	.read_requested = &smbus_read_handler,
	.read_processed = &smbus_read_handler,
	.stop = &smbus_stop_handler,
};

static int32_t smbus_target_init(const struct device *dev)
{
	struct smbus_target_data *data = dev->data;
	const struct smbus_target_config *cfg = dev->config;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C controller device not ready");
		return -ENODEV;
	}

	data->config.address = cfg->bus.addr;
	data->config.callbacks = &smbus_target_cb_impl;

	return 0;
}

int32_t smbus_target_register_cmd(const struct device *dev, uint8_t cmd_id,
				  const SmbusCmdDef *smbus_cmd)
{
	struct smbus_target_data *data = dev->data;

	if (!device_is_ready(dev)) {
		LOG_ERR("SMBUS device not ready");
		return -ENODEV;
	}

	data->cmd_defs[cmd_id] = smbus_cmd;
	return 0;
}

#define SMBUS_TARGET_INIT(inst)                                                                    \
	static struct smbus_target_data smbus_target_##inst##_dev_data = {0};                      \
	static const struct smbus_target_config smbus_target_##inst##_cfg = {                      \
		.bus = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &smbus_target_init, NULL, &smbus_target_##inst##_dev_data,     \
			      &smbus_target_##inst##_cfg, POST_KERNEL,                             \
			      CONFIG_I2C_TARGET_INIT_PRIORITY, &api_funcs);

DT_INST_FOREACH_STATUS_OKAY(SMBUS_TARGET_INIT)
