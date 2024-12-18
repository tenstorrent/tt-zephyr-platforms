/*
 * Copyright (c) 2024, Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file cm2bm_msg.c
 * @brief CMFW to BMFW message handling
 *
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "cm2bm_msg.h"
#include "asic_state.h"
#include "telemetry.h"

typedef struct {
  uint8_t curr_msg_valid;
  uint8_t next_seq_num;
  Cm2BmSmbusReqMsg curr_msg;
} Cm2BmMsgState;

static Cm2BmMsgState cm2bm_msg_state;
K_MSGQ_DEFINE(cm2bm_msg_q, sizeof(Cm2BmMsg), 4, _Alignof(Cm2BmMsg));

int32_t EnqueueCm2BmMsg(const Cm2BmMsg *msg) {
  // May be called from ISR context, so keep this function ISR-safe
  return k_msgq_put(&cm2bm_msg_q, msg, K_NO_WAIT);
}

int32_t Cm2BmMsgReqSmbusHandler(uint8_t *data, uint8_t size) {
  BUILD_ASSERT(sizeof(cm2bm_msg_state.curr_msg) == 6, "Unexpected size of cm2bm_msg_state.curr_msg");
  if (size != sizeof(cm2bm_msg_state.curr_msg))
    return -1;

  if (!cm2bm_msg_state.curr_msg_valid) {
    // See if there is a message in the queue
    Cm2BmMsg msg;
    if (k_msgq_get(&cm2bm_msg_q, &msg, K_NO_WAIT) != 0) {
      // Send the all zero message if the message queue is empty
      memset(data, 0, sizeof(cm2bm_msg_state.curr_msg));
      return 0;
    }

    // If there is a valid message, copy it over to the current message
    cm2bm_msg_state.curr_msg_valid = 1;
    cm2bm_msg_state.curr_msg.msg_id = msg.msg_id;
    cm2bm_msg_state.curr_msg.seq_num = cm2bm_msg_state.next_seq_num++;
    cm2bm_msg_state.curr_msg.data = msg.data;
  }
  memcpy(data, &cm2bm_msg_state.curr_msg, sizeof(cm2bm_msg_state.curr_msg));
  return 0;
}

int32_t Cm2BmMsgAckSmbusHandler(const uint8_t *data, uint8_t size) {
  BUILD_ASSERT(sizeof(Cm2BmSmbusAckMsg) == 2, "Unexpected size of Cm2BmSmbusAckMsg");
  if (size != sizeof(Cm2BmSmbusAckMsg))
    return -1;

  Cm2BmSmbusAckMsg *ack = (Cm2BmSmbusAckMsg *)data;
  if (cm2bm_msg_state.curr_msg_valid &&
      ack->msg_id == cm2bm_msg_state.curr_msg.msg_id &&
      ack->seq_num == cm2bm_msg_state.curr_msg.seq_num) {
    // Message has been handled when msg_id and seq_num match the current valid message
    cm2bm_msg_state.curr_msg_valid = 0;
    return 0;
  } else {
    return -1;
  }
}

void IssueChipReset(uint32_t reset_level) {
  lock_down_for_reset();

  // Send a reset request to the BMFW
  Cm2BmMsg msg = {
    .msg_id = kCm2BmMsgIdResetReq,
    .data = reset_level,
  };
  EnqueueCm2BmMsg(&msg);
}

void ChipResetRequest(void *arg) {
  if (arg != NULL) {
    uint32_t irq_num = POINTER_TO_UINT(arg);
    irq_disable(irq_num); // So we don't get repeatedly interrupted
  }

  IssueChipReset(0);
}

// Report the current message and automatically acknowledge it.
int32_t ResetBoardByte(uint8_t *data, uint8_t size) {
  memset(data, 0, size);

  if (!cm2bm_msg_state.curr_msg_valid) {
    // See if there is a message in the queue
    Cm2BmMsg msg;
    if (k_msgq_get(&cm2bm_msg_q, &msg, K_NO_WAIT) != 0) {
      // Send the all zero message if the message queue is empty
      *data = 0;
      return 0;
    }

    // If there is a valid message, copy it over to the current message
    cm2bm_msg_state.curr_msg_valid = 1;
    cm2bm_msg_state.curr_msg.msg_id = msg.msg_id;
    cm2bm_msg_state.curr_msg.seq_num = cm2bm_msg_state.next_seq_num++;
    cm2bm_msg_state.curr_msg.data = msg.data;
  }
  *data = cm2bm_msg_state.curr_msg.msg_id;

  // Because there's no acknowledgement coming, remove the message.
  cm2bm_msg_state.curr_msg_valid = 0;

  return 0;
}

static uint8_t reset_bm_handler(uint32_t msg_code, const struct request *request, struct response *response) {
  uint8_t arg = request->data[1];

  // Don't expect a response from the bmfw so need to check here for a valid reset level
  uint8_t ret = 0;
  switch (arg) {
    case 0:
    case 3:
      IssueChipReset(arg);
      break;
    default:
      // Can never be zero because that case is covered by asic reset
      ret = arg;
  }

  return ret;
}

REGISTER_MESSAGE(MSG_TYPE_TRIGGER_RESET, reset_bm_handler);

int32_t Bm2CmSendDataHandler(const uint8_t *data, uint8_t size)
{
  if (size != sizeof(bmStaticInfo))
    return -1;

  bmStaticInfo *info = (bmStaticInfo *)data;
  if (info->version != 0) {
    UpdateBmFwVersion(info->bl_version, info->app_version);
    return 0;
  } else {
    return -1;
  }
}
