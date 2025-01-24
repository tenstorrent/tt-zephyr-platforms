/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_MSGQUEUE_H_
#define TENSTORRENT_MSGQUEUE_H_

#include <stdint.h>

#include <zephyr/sys/iterable_sections.h>

#define NUM_MSG_QUEUES         4
#define MSG_QUEUE_SIZE         4
#define MSG_QUEUE_POINTER_WRAP (2 * MSG_QUEUE_SIZE)
#define REQUEST_MSG_LEN        8
#define RESPONSE_MSG_LEN       8

#define MSG_TYPE_INDEX 0
#define MSG_TYPE_MASK  0xFF
#define MSG_TYPE_SHIFT 0

#define MESSAGE_QUEUE_STATUS_MESSAGE_RECOGNIZED 0xff
#define MESSAGE_QUEUE_STATUS_SCRATCH_ONLY       0xfe

#ifdef __cplusplus
extern "C" {
#endif

struct message_queue_header {
	/* 16B for CPU writes, ARC reads */
	uint32_t request_queue_wptr;
	uint32_t response_queue_rptr;
	uint32_t unused_1;
	uint32_t unused_2;

	/* 16B for ARC writes, CPU reads */
	uint32_t request_queue_rptr;
	uint32_t response_queue_wptr;
	uint32_t last_serial;
	uint32_t unused_3;
};

struct request {
	uint32_t data[REQUEST_MSG_LEN];
};

struct response {
	uint32_t data[RESPONSE_MSG_LEN];
};

typedef uint8_t (*msgqueue_request_handler_t)(uint32_t msg_code, const struct request *req,
					      struct response *rsp);

struct msgqueue_handler {
	uint32_t msg_type;
	msgqueue_request_handler_t handler;
};

#define REGISTER_MESSAGE(msg, func)                                                                \
	const STRUCT_SECTION_ITERABLE(msgqueue_handler, registration_for_##msg) = {                \
		.msg_type = msg,                                                                   \
		.handler = func,                                                                   \
	}

void process_message_queues(void);
void msgqueue_register_handler(uint32_t msg_code, msgqueue_request_handler_t handler);

int msgqueue_request_push(uint32_t msgqueue_id, const struct request *request);
int msgqueue_request_pop(uint32_t msgqueue_id, struct request *request);
int msgqueue_response_push(uint32_t msgqueue_id, const struct response *response);
int msgqueue_response_pop(uint32_t msgqueue_id, struct response *response);
void init_msgqueue(void);

#ifdef __cplusplus
}
#endif

#endif
