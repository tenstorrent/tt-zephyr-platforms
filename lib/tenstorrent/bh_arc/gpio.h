/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

#define GPIO_THERM_TRIP         31
#define GPIO_PCIE_TRISTATE_CTRL 34
#define GPIO_CEM0_PERST         37

void GpioEnableOutput(uint32_t pin);
void GpioDisableOutput(uint32_t pin);
void GpioRxEnable(uint32_t pin);
void GpioRxDisable(uint32_t pin);
void GpioSet(uint32_t pin, uint32_t val);

#endif
