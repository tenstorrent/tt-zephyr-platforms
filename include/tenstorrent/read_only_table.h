/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_READ_ONLY_TABLE_H_
#define TENSTORRENT_READ_ONLY_TABLE_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Enum describing PCB/board types (must match bh_arc implementation) */
typedef enum {
	PcbTypeOrion = 0,
	PcbTypeP100 = 1,
	PcbTypeP150 = 2,
	PcbTypeP300 = 3,
	PcbTypeUBB = 4,
	PcbTypeUnknown = 0xFF
} PcbType;

/* Forward-declare the nanopb-generated structure so callers can hold a const* */
typedef struct ReadOnly ReadOnly;

int load_read_only_table(uint8_t *buffer_space, uint32_t buffer_size);
const ReadOnly *get_read_only_table(void);
PcbType get_pcb_type(void);

/*
 * For firmware that does not link the full bh_arc library (e.g. DMC), provide
 * a lightweight inline implementation of is_p300_left_chip().  When the full
 * library is present this will be optimised away in favour of the external
 * definition, but if it is absent the inline keeps the symbol resolved at
 * compile-time (ODR-safe because it is 'static inline').
 */
static inline bool is_p300_left_chip(void)
{
	/* GPIO6 strap (bit 6) is only tied high on the P300 left-side chip. */
#define TT_RESET_UNIT_STRAP_REG_L 0x80030D20
	return FIELD_GET(BIT(6), TT_RESET_UNIT_STRAP_REG_L);
}
uint32_t get_asic_location(void);

#ifdef __cplusplus
}
#endif

#endif /* TENSTORRENT_READ_ONLY_TABLE_H_ */
