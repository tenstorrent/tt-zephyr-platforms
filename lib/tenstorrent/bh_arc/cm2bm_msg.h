/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CM2BM_MSG_H
#define CM2BM_MSG_H

#include <stdint.h>
#include <zephyr/toolchain.h>

typedef enum {
	kCm2BmMsgIdNull = 0,
	kCm2BmMsgIdResetReq = 1,
	kCm2BmMsgIdPing = 2,
	kCm2BmMsgIdFanSpeedUpdate = 3,
	kCm2BmMsgIdAutoRstTimeoutUpdate = 4,
	kCm2BmMsgTelemHeartbeatUpdate = 5,
} Cm2BmMsgId;

typedef struct {
	uint8_t msg_id;
	uint32_t data;
} Cm2BmMsg;

typedef struct {
	uint8_t msg_id;
	uint8_t seq_num;
	uint32_t data;
} __packed Cm2BmSmbusReqMsg;

typedef struct {
	uint8_t msg_id;
	uint8_t seq_num;
} __packed Cm2BmSmbusAckMsg;

int32_t EnqueueCm2BmMsg(const Cm2BmMsg *msg);
int32_t Cm2BmMsgReqSmbusHandler(uint8_t *data, uint8_t size);
int32_t Cm2BmMsgAckSmbusHandler(const uint8_t *data, uint8_t size);
int32_t ResetBoardByte(uint8_t *data, uint8_t size);

void ChipResetRequest(void *arg);
void UpdateFanSpeedRequest(uint32_t fan_speed);
void UpdateAutoRstTimeoutRequest(uint32_t timeout);
void UpdateTelemHeartbeatRequest(uint32_t heartbeat);

typedef struct bmStaticInfo {
	/* Non-zero for valid data */
	/* Allows for breaking changes */
	uint32_t version;
	uint32_t bl_version;
	uint32_t app_version;
} __packed bmStaticInfo;

int32_t Bm2CmSendDataHandler(const uint8_t *data, uint8_t size);
int32_t Bm2CmPingHandler(const uint8_t *data, uint8_t size);
int32_t Bm2CmSendCurrentHandler(const uint8_t *data, uint8_t size);
int32_t GetInputCurrent(void);
int32_t Bm2CmSendFanRPMHandler(const uint8_t *data, uint8_t size);

#endif
