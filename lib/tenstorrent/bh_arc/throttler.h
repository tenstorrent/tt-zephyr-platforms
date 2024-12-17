/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THROTTLER_H
#define THROTTLER_H

void InitThrottlers(void);
void CalculateThrottlers(void);
int32_t Bm2CmSetBoardPwrLimit(const uint8_t *data, uint8_t size);

#endif
