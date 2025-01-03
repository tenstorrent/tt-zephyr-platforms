/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DW_APB_UART_H
#define DW_APB_UART_H

#include <stdint.h>

void UartInit(void);
void UartTransmitFrames(uint32_t num_frame, uint8_t *data);
uint8_t UartReceiveFrame(void);
#endif
