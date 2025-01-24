/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

static uint8_t msgqueue_handler_73(uint32_t msg_code, const struct request *req,
				   struct response *rsp)
{
	BUILD_ASSERT(MSG_TYPE_SHIFT % 8 == 0);
	rsp->data[1] = req->data[0];
	return 0;
}

ZTEST(msgqueue, test_msgqueue_register_handler)
{
	struct request req = {0};
	struct response rsp = {0};

	msgqueue_register_handler(0x73, msgqueue_handler_73);

	req.data[0] = 0x73737373;
	msgqueue_request_push(0, &req);
	process_message_queues();
	msgqueue_response_pop(0, &rsp);

	zassert_equal(rsp.data[1], 0x73737373);
}

ZTEST_SUITE(msgqueue, NULL, NULL, NULL, NULL, NULL);
