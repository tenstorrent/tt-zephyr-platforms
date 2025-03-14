/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define GPIO_XP_REG_INPUT_PORT 0x0 /* Read input values */
#define GPIO_XP_REG_OUTPUT_PORT 0x1 /* Read/write output values */
#define GPIO_XP_REG_POLARITY_INV 0x2
#define GPIO_XP_REG_CONFIG 0x3 /* Configure GPIOs, output = 0 and input = 1 */

/**
 * @brief Set up GPIOs on GPIO expander to enable high power mode on QSFP module
 *
 * @retval 0 on success.
 * @retval -EIO	if an I/O error occurs.
 */
int enable_active_qsfp_dd(void);
