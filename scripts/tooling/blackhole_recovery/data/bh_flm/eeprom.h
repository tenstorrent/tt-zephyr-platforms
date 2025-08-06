/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* This file defines the programming routines for
 * SPI eeproms.
 */

#include <stdint.h>

#ifndef EEPROM_H
#define EEPROM_H

int eeprom_init(void);

int eeprom_deinit(void);

int eeprom_erase_chip(void);

int eeprom_erase_sector(uint32_t sector);

int eeprom_program(uint32_t addr, const uint8_t *data, uint32_t len);

int eeprom_read(uint32_t addr, uint8_t *data, uint32_t len);

#endif /* EEPROM_H */
