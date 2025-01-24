/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_BH_ARC_UTIL_H_
#define TENSTORRENT_BH_ARC_UTIL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t low32(uint64_t val)
{
	return val & 0xFFFFFFFF;
}

static inline uint32_t high32(uint64_t val)
{
	return val >> 32;
}

static inline void FlipBytes(uint8_t *buf, uint32_t byte_size)
{
	uint32_t temp = 0;

	for (uint32_t i = 0; i < byte_size / 2; i++) {
		temp = buf[i];
		buf[i] = buf[byte_size - 1 - i];
		buf[byte_size - 1 - i] = temp;
	}
}

#ifdef __cplusplus
}
#endif

#endif
