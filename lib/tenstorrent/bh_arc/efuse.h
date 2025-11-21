/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef EFUSE_H
#define EFUSE_H

#include <stdint.h>

typedef enum {
	EfuseBoxDft0 = 0,
	EfuseBoxDft1 = 1,
	EfuseBoxFunc = 2,
	EfuseBoxIdNum = 3,
} EfuseBoxId;

typedef enum {
	EfuseIndirect = 0, /* indirect access by programming read control registers */
	EfuseDirect = 1,   /* direct access by reading efuse box */
} EfuseAccessType;

uint32_t EfuseRead(EfuseAccessType acc_type, EfuseBoxId efuse_box_id, uint32_t offset);

#endif
