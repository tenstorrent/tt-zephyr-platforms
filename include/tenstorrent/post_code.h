/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_POST_CODE_H_
#define TENSTORRENT_POST_CODE_H_

#include <stdint.h>

#define POST_CODE_PREFIX 0xc0de

/* Post code sources */
#define POST_CODE_SRC_CMFW 0x0

/* List of post codes */
#define POST_CODE_ARC_INIT_STEP0   0x10
#define POST_CODE_ARC_INIT_STEP1   0x11
#define POST_CODE_ARC_INIT_STEP2   0x12
#define POST_CODE_ARC_INIT_STEP3   0x13
#define POST_CODE_ARC_INIT_STEP4   0x14
#define POST_CODE_ARC_INIT_STEP5   0x15
#define POST_CODE_ARC_INIT_STEP6   0x16
#define POST_CODE_ARC_INIT_STEP7   0x17
#define POST_CODE_ARC_INIT_STEP8   0x18
#define POST_CODE_ARC_INIT_STEP9   0x19
#define POST_CODE_ARC_INIT_STEPA   0x1A
#define POST_CODE_ARC_INIT_STEPB   0x1B
#define POST_CODE_ARC_INIT_STEPC   0x1C
#define POST_CODE_ARC_INIT_STEPD   0x1D

#define POST_CODE_ZEPHYR_INIT_DONE 0x20

#define POST_CODE_ARC_MSG_HANDLE_START 0x30
#define POST_CODE_ARG_MSG_QUEUE_START  0x31
/* values 0x31-0x3E are used for message queues 0-13 */
#define POST_CODE_ARG_MSG_QUEUE(i)     (POST_CODE_ARG_MSG_QUEUE_START + (i))
#define POST_CODE_ARC_MSG_HANDLE_DONE  0x3F

#define POST_CODE_TELEMETRY_START 0x40
#define POST_CODE_TELEMETRY_END   0x41

#ifdef __cplusplus
extern "C" {
#endif

void SetPostCode(uint8_t fw_id, uint16_t post_code);

#ifdef __cplusplus
}
#endif

#endif
