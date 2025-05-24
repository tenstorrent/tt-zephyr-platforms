/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_LIB_BH_ARC_H_
#define INCLUDE_TENSTORRENT_LIB_BH_ARC_H_

#include <stdint.h>

#include <zephyr/drivers/smbus.h>
#include <zephyr/drivers/gpio.h>

typedef enum {
	kCm2DmMsgIdNull = 0,
	kCm2DmMsgIdResetReq = 1,
	kCm2DmMsgIdPing = 2,
	kCm2DmMsgIdFanSpeedUpdate = 3,
	kCm2DmMsgIdReady = 4,
	kCm2DmMsgIdAutoResetTimeoutUpdate = 5,
	kCm2DmMsgTelemHeartbeatUpdate = 6,
} Cm2DmMsgId;

typedef struct dmStaticInfo {
	/*
	 * Non-zero for valid data
	 * Allows for breaking changes
	 */
	uint32_t version;
	uint32_t bl_version;
	uint32_t app_version;
	uint32_t last_reset_was_automatic;
} __packed dmStaticInfo;

typedef struct cm2dmMessage {
	uint8_t msg_id;
	uint8_t seq_num;
	uint32_t data;
} __packed cm2dmMessage;

typedef struct cm2dmAck {
	uint8_t msg_id;
	uint8_t seq_num;
} __packed cm2dmAck;

union cm2dmAckWire {
	cm2dmAck f;
	uint16_t val;
};

struct bh_arc {
	const struct smbus_dt_spec smbus;
	const struct gpio_dt_spec enable;
};

typedef struct cm2dmMessageRet {
	cm2dmMessage msg;
	int ret;

	cm2dmAck ack;
	int ack_ret;
} cm2dmMessageRet;

int bharc_smbus_block_read(const struct bh_arc *dev, uint8_t cmd, uint8_t *count, uint8_t *output);
int bharc_smbus_block_write(const struct bh_arc *dev, uint8_t cmd, uint8_t count, uint8_t *input);
int bharc_smbus_word_data_write(const struct bh_arc *dev, uint16_t cmd, uint16_t word);
int bharc_smbus_byte_data_write(const struct bh_arc *dev, uint8_t cmd, uint8_t word);

#define BH_ARC_INIT(n)                                                                             \
	{.smbus = SMBUS_DT_SPEC_GET(n),                                                            \
	 .enable = COND_CODE_1(DT_PROP_HAS_IDX(n, gpios, 0),	({	\
			.port = DEVICE_DT_GET(DT_GPIO_CTLR_BY_IDX(n, gpios, 0)),                   \
			.pin = DT_GPIO_PIN_BY_IDX(n, gpios, 0),                                    \
			.dt_flags = DT_GPIO_FLAGS_BY_IDX(n, gpios, 0),                             \
		}), ({})) }

#endif
