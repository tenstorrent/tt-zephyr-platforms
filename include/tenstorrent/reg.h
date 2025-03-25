/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REG_H
#define REG_H

#include <stdint.h>

static inline uint32_t ReadReg(uint32_t addr)
{
	return *((uint32_t volatile *)addr);
}
static inline void WriteReg(uint32_t addr, uint32_t val)
{
	*((uint32_t volatile *)addr) = val;
}
#endif
