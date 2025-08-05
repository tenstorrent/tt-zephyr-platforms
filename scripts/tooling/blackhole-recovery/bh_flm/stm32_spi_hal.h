/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STM32_SPI_HAL_H
#define STM32_SPI_HAL_H

#include <stdint.h>

int stm32_spi_init(void);
int stm32_spi_deinit(void);
int stm32_spi_transfer(struct spi_buf *bufs, uint8_t cnt);

#endif /* STM32_SPI_HAL_H */
