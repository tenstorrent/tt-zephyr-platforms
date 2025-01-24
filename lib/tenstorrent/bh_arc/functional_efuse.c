/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/sys/util.h>
#include "functional_efuse.h"
#include "efuse.h"

/* Extracts fields from the functional efuse from start_bit to end_bit (inclusive) */
/* Note that this only works for fields that are 32-bits or smaller */
/* i.e. end_bit - start_bit < 32 */
uint32_t ReadFunctionalEfuse(uint32_t start_bit, uint32_t end_bit)
{
	int32_t field_length = end_bit - start_bit + 1;

	if (field_length > 32 || field_length < 1) {
		/* These are error cases, just return 0 */
		return 0;
	}

	uint32_t start_index = start_bit / 32;
	uint64_t data = 0;

	/* We must read 4 bytes at a time as a uint32_t */
	/* But we want to handle the case where a field spans across two dwords */
	data = EfuseRead(EfuseDirect, EfuseBoxFunc, start_index);
	data |= (uint64_t)EfuseRead(EfuseDirect, EfuseBoxFunc, start_index + 1) << 32;
	/* Corner case: this will read past the end of the functional efuse */
	/* when we try to access the last dword, */
	/* but that case should be safe from the HW perspective */

	/* Mask and shift the bits we want */
	uint64_t mask = GENMASK64(field_length + (start_bit % 32) - 1, start_bit % 32);

	return FIELD_GET(mask, data);
}
