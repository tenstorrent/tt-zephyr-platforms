/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CM2DM_MSG_H
#define CM2DM_MSG_H

#include <stdint.h>
#include <zephyr/toolchain.h>
#include <tenstorrent/bh_arc.h>

typedef struct {
	uint8_t msg_id;
	uint32_t data;
} Cm2DmMsg;

int32_t EnqueueCm2DmMsg(const Cm2DmMsg *msg);
int32_t Cm2DmMsgReqSmbusHandler(uint8_t *data, uint8_t size);
int32_t Cm2DmMsgAckSmbusHandler(const uint8_t *data, uint8_t size);
int32_t ResetBoardByte(uint8_t *data, uint8_t size);

void ChipResetRequest(void *arg);
void UpdateFanSpeedRequest(uint32_t fan_speed);
void Dm2CmReadyRequest(void);

int32_t Dm2CmSendDataHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmPingHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmSendCurrentHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmSendPowerHandler(const uint8_t *data, uint8_t size);
int32_t Dm2CmDebugDataHandler(const uint8_t *data, uint8_t size);
int32_t GetInputCurrent(void);
uint16_t GetInputPower(void);
int32_t Dm2CmSendFanRPMHandler(const uint8_t *data, uint8_t size);
int32_t SMBusTelemRegHandler(const uint8_t *data, uint8_t size);
int32_t SMBusTelemDataHandler(uint8_t *data, uint8_t size);

#endif
