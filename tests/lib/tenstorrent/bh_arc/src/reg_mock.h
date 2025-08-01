/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef REG_MOCK_H
#define REG_MOCK_H
#include <zephyr/fff.h>
#include <stdint.h>

DECLARE_FAKE_VALUE_FUNC(uint32_t, ReadReg, uint32_t /*addr*/);
DECLARE_FAKE_VOID_FUNC(WriteReg, uint32_t /*addr*/, uint32_t /*val*/);
#endif /*REG_MOCK_H*/
