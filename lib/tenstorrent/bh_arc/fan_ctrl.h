/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FAN_CTRL_H
#define FAN_CTRL_H

#include <stdint.h>

void init_fan_ctrl(void);
uint32_t GetFanSpeed(void);
uint16_t GetFanRPM(void);
void SetFanRPM(uint16_t rpm);

/*
 * Apply a board-level forced fan speed coming from the DMFW. The value is a percentage in the
 * range 0-100. A value of 0 means "unforce" (return to automatic control).
 */
void FanCtrlApplyBoardForcedSpeed(uint32_t speed_percentage);

#endif
