/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CM2DM_MSG_H
#define CM2DM_MSG_H

#include <stdint.h>
#include <zephyr/toolchain.h>

typedef enum {
	kCm2DmMsgIdNull = 0,
	kCm2DmMsgIdResetReq = 1,
	kCm2DmMsgIdPing = 2,
	kCm2DmMsgIdFanSpeedUpdate = 3,
	kCm2DmMsgIdReady = 4,
} Cm2DmMsgId;

typedef struct {
	uint8_t msg_id;
	uint32_t data;
} Cm2DmMsg;

typedef struct {
	uint8_t msg_id;
	uint8_t seq_num;
	uint32_t data;
} __packed Cm2DmSmbusReqMsg;

typedef struct {
	uint8_t msg_id;
	uint8_t seq_num;
} __packed Cm2DmSmbusAckMsg;

int32_t EnqueueCm2DmMsg(const Cm2DmMsg *msg);
int32_t Cm2DmMsgReqSmbusHandler(uint8_t *data, uint8_t size);
int32_t Cm2DmMsgAckSmbusHandler(const uint8_t *data, uint8_t size);
int32_t ResetBoardByte(uint8_t *data, uint8_t size);

void ChipResetRequest(void *arg);
void UpdateFanSpeedRequest(uint32_t fan_speed);
void Dm2CmReadyRequest(void);

typedef struct dmStaticInfo {
	/* Non-zero for valid data */
	/* Allows for breaking changes */
	uint32_t version;
	uint32_t bl_version;
	uint32_t app_version;
} __packed dmStaticInfo;

int32_t Dm2CmSendDataHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmPingHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmSendCurrentHandler(const uint8_t *data, uint8_t size);
int32_t GetInputCurrent(void);
int32_t Dm2CmSendFanRPMHandler(const uint8_t *data, uint8_t size);

#endif
