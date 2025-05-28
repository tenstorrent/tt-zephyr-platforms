/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cm2dm_msg.c
 * @brief CMFW to DMFW message handling
 *
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "cm2dm_msg.h"
#include "asic_state.h"
#include "fan_ctrl.h"
#include "telemetry.h"

typedef struct {
	uint8_t curr_msg_valid;
	uint8_t next_seq_num;
	cm2dmMessage curr_msg;
} Cm2DmMsgState;

static Cm2DmMsgState cm2dm_msg_state;
static bool dmfw_ping_valid;
static uint16_t power;
static uint16_t telemetry_reg;
K_MSGQ_DEFINE(cm2dm_msg_q, sizeof(Cm2DmMsg), 4, _Alignof(Cm2DmMsg));

int32_t EnqueueCm2DmMsg(const Cm2DmMsg *msg)
{
	/* May be called from ISR context, so keep this function ISR-safe */
	return k_msgq_put(&cm2dm_msg_q, msg, K_NO_WAIT);
}

int32_t Cm2DmMsgReqSmbusHandler(uint8_t *data, uint8_t size)
{
	BUILD_ASSERT(sizeof(cm2dm_msg_state.curr_msg) == 6,
		     "Unexpected size of cm2dm_msg_state.curr_msg");
	if (size != sizeof(cm2dm_msg_state.curr_msg)) {
		return -1;
	}

	if (!cm2dm_msg_state.curr_msg_valid) {
		/* See if there is a message in the queue */
		Cm2DmMsg msg;

		if (k_msgq_get(&cm2dm_msg_q, &msg, K_NO_WAIT) != 0) {
			/* Send the all zero message if the message queue is empty */
			memset(data, 0, sizeof(cm2dm_msg_state.curr_msg));
			return 0;
		}

		cm2dm_msg_state.curr_msg_valid = 1;
		cm2dm_msg_state.curr_msg.msg_id = msg.msg_id;
		cm2dm_msg_state.curr_msg.seq_num = cm2dm_msg_state.next_seq_num++;
		cm2dm_msg_state.curr_msg.data = msg.data;
	}
	memcpy(data, &cm2dm_msg_state.curr_msg, sizeof(cm2dm_msg_state.curr_msg));
	return 0;
}

int32_t Cm2DmMsgAckSmbusHandler(const uint8_t *data, uint8_t size)
{
	BUILD_ASSERT(sizeof(cm2dmAck) == 2, "Unexpected size of cm2dmAck");
	if (size != sizeof(cm2dmAck)) {
		return -1;
	}

	cm2dmAck *ack = (cm2dmAck *)data;

	if (cm2dm_msg_state.curr_msg_valid && ack->msg_id == cm2dm_msg_state.curr_msg.msg_id &&
	    ack->seq_num == cm2dm_msg_state.curr_msg.seq_num) {
		/* Message handled when msg_id and seq_num match the current valid message */
		cm2dm_msg_state.curr_msg_valid = 0;
		return 0;
	} else {
		return -1;
	}
}

void IssueChipReset(uint32_t reset_level)
{
	lock_down_for_reset();

	/* Send a reset request to the DMFW */
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdResetReq,
		.data = reset_level,
	};
	EnqueueCm2DmMsg(&msg);
}

void ChipResetRequest(void *arg)
{
	if (arg != NULL) {
		uint32_t irq_num = POINTER_TO_UINT(arg);

		irq_disable(irq_num); /* So we don't get repeatedly interrupted */
	}

	IssueChipReset(0);
}

void UpdateFanSpeedRequest(uint32_t fan_speed)
{
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdFanSpeedUpdate,
		.data = fan_speed,
	};
	EnqueueCm2DmMsg(&msg);
}

void Dm2CmReadyRequest(void)
{
	/* Send a message to dmfw to indicate ready to receive messages */
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdReady,
	};
	EnqueueCm2DmMsg(&msg);
}

void UpdateAutoResetTimeoutRequest(uint32_t timeout)
{
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdAutoResetTimeoutUpdate, .data = timeout /* in ms */
	};
	EnqueueCm2DmMsg(&msg);
}

void UpdateTelemHeartbeatRequest(uint32_t heartbeat)
{
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgTelemHeartbeatUpdate, .data = heartbeat /* in ms */
	};
	EnqueueCm2DmMsg(&msg);
}

/* Report the current message and automatically acknowledge it. */
int32_t ResetBoardByte(uint8_t *data, uint8_t size)
{
	memset(data, 0, size);

	if (!cm2dm_msg_state.curr_msg_valid) {
		/* See if there is a message in the queue */
		Cm2DmMsg msg;

		if (k_msgq_get(&cm2dm_msg_q, &msg, K_NO_WAIT) != 0) {
			/* Send the all zero message if the message queue is empty */
			*data = 0;
			return 0;
		}

		/* If there is a valid message, copy it over to the current message */
		cm2dm_msg_state.curr_msg_valid = 1;
		cm2dm_msg_state.curr_msg.msg_id = msg.msg_id;
		cm2dm_msg_state.curr_msg.seq_num = cm2dm_msg_state.next_seq_num++;
		cm2dm_msg_state.curr_msg.data = msg.data;
	}
	*data = cm2dm_msg_state.curr_msg.msg_id;

	/* Because there's no acknowledgment coming, remove the message. */
	cm2dm_msg_state.curr_msg_valid = 0;

	return 0;
}

static uint8_t reset_dm_handler(uint32_t msg_code, const struct request *request,
				struct response *response)
{
	uint8_t arg = request->data[1];

	/* Don't expect a response from the dmfw so need to check here for a valid reset level */
	uint8_t ret = 0;

	switch (arg) {
	case 0:
	case 3:
		IssueChipReset(arg);
		break;
	default:
		/* Can never be zero because that case is covered by asic reset */
		ret = arg;
	}

	return ret;
}

REGISTER_MESSAGE(MSG_TYPE_TRIGGER_RESET, reset_dm_handler);

static uint8_t ping_dm_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	/* Send a ping to the dmfw */
	Cm2DmMsg msg = {
		.msg_id = kCm2DmMsgIdPing,
	};

	dmfw_ping_valid = false;
	EnqueueCm2DmMsg(&msg);
	/* Delay to allow DMFW to respond */
	k_msleep(50);

	/* Encode response from DMFW */
	response->data[1] = dmfw_ping_valid;
	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_PING_DM, ping_dm_handler);

int32_t Dm2CmSendDataHandler(const uint8_t *data, uint8_t size)
{
#ifndef CONFIG_TT_SMC_RECOVERY
	if (size != sizeof(dmStaticInfo)) {
		return -1;
	}

	dmStaticInfo *info = (dmStaticInfo *)data;

	if (info->version != 0) {
		UpdateDmFwVersion(info->bl_version, info->app_version);
		return 0;
	}
#endif

	return -1;
}

int32_t Dm2CmPingHandler(const uint8_t *data, uint8_t size)
{
	if (size != 2) {
		return -1;
	}

	uint16_t response = *(uint16_t *)data;

	if (response != 0xA5A5) {
		dmfw_ping_valid = false;
		return -1;
	}
	dmfw_ping_valid = true;
	return 0;
}

int32_t Dm2CmSendPowerHandler(const uint8_t *data, uint8_t size)
{
	if (size != 2) {
		return -1;
	}

	power = sys_get_le16(data);

	return 0;
}

/* TODO: Put these somewhere else? */

uint16_t GetInputPower(void)
{
	return power;
}

int32_t Dm2CmSendFanRPMHandler(const uint8_t *data, uint8_t size)
{
#ifndef CONFIG_TT_SMC_RECOVERY
	if (size != 2) {
		return -1;
	}

	SetFanRPM(sys_get_le16(data));

	return 0;
#endif

	return -1;
}

int32_t SMBusTelemRegHandler(const uint8_t *data, uint8_t size)
{
	if (size != 1) {
		return -1;
	}

	/* Load telemetry register with data */
	telemetry_reg = data[0];
	return 0;
}

int32_t SMBusTelemDataHandler(uint8_t *data, uint8_t size)
{
	uint32_t telemetry_data;

	if (size != sizeof(telemetry_data)) {
		return -1;
	}

	telemetry_data = GetTelemetryTag(telemetry_reg);
	memcpy(data, &telemetry_data, sizeof(telemetry_data));
	return 0;
}
