/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef READ_ONLY_TABLE_H
#define READ_ONLY_TABLE_H

#include "spirom_protobufs/read_only.pb.h"

typedef enum {
	PcbTypeOrion = 0,
	PcbTypeP100 = 1,
	PcbTypeP150 = 2,
	PcbTypeP300 = 3,
	PcbTypeUBB = 4,
	PcbTypeUnknown = 0xFF
} PcbType;

/* Function declarations */
int load_read_only_table(uint8_t *buffer_space, uint32_t buffer_size);
const ReadOnly *get_read_only_table(void);
PcbType get_pcb_type(void);
uint32_t get_asic_location(void);

#endif /* READ_ONLY_TABLE_H */
