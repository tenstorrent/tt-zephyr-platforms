/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FW_TABLE_H
#define FW_TABLE_H

#include "spirom_protobufs/fw_table.pb.h"

/* Function declarations */
int load_fw_table(uint8_t *buffer_space, uint32_t buffer_size);
const FwTable *get_fw_table(void);

#endif /* FW_TABLE_H */
