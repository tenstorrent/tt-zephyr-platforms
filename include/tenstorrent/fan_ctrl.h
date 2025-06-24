/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

/**
 * @brief Set target fan speed on fan controller.
 *
 * @retval 0 on success.
 * @retval -EIO if an I/O error occurs.
 */
int set_fan_speed(uint8_t fan_speed);

/**
 * @brief Get current fan duty cycle in percentage.
 *
 * @retval Fan speed percentage.
 */
uint8_t get_fan_duty_cycle(void);

/**
 * @brief Get current fan RPM from fan tachometer.
 *
 * @retval Fan RPM.
 */
uint16_t get_fan_rpm(void);
