/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FLASH_INFO_TABLE_H
#define FLASH_INFO_TABLE_H

#include "spirom_protobufs/flash_info.pb.h"

/* Function declarations */
int load_flash_info_table(uint8_t *buffer_space, uint32_t buffer_size);
const FlashInfoTable *get_flash_info_table(void);

#endif /* FLASH_INFO_TABLE_H */
