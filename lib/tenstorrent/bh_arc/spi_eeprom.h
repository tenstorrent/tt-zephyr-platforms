/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPI_EEPROM_H
#define SPI_EEPROM_H

#include <stdint.h>

void SpiEepromSetup(void);
void ReinitSpiclk(void);

#endif
