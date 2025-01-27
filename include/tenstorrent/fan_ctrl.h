/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#define FAN_CTRL_ADDR   0x2C
#define GLOBAL_CONFIG   0x04
#define FAN1_CONFIG_1   0x10
#define FAN1_CONFIG_2A  0x11
#define FAN1_CONFIG_3   0x13
#define FAN1_DUTY_CYCLE 0x26

/**
 * @brief Fan controller initialization sequence.
 *
 * @retval 0 on success.
 * @retval -EIO	if an I/O error occurs.
 */
int init_fan(void);

/**
 * @brief Set target fan speed on fan controller.
 *
 * @retval 0 on success.
 * @retval -EIO if an I/O error occurs.
 */
int set_fan_speed(uint8_t fan_speed);

/**
 * @brief Get current fan speed from fan controller.
 *
 * @retval Fan speed percentage.
 */
uint8_t get_fan_speed(void);
