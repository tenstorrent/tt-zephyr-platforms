/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_BITREV_H_
#define INCLUDE_TENSTORRENT_BITREV_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint8_t bitrev4(uint8_t nibble)
{
	const uint8_t rev4[] = {0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
				0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};

	return rev4[nibble & 0xf];
}

static inline uint8_t bitrev8(uint8_t byte)
{
	return (bitrev4(byte) << 4) | bitrev4(byte >> 4);
}

static inline uint16_t bitrev16(uint16_t hword)
{
	return ((uint16_t)bitrev8(hword) << 8) | bitrev8(hword >> 8);
}

static inline uint32_t bitrev32(uint32_t word)
{
	return ((uint32_t)bitrev16(word) << 16) | bitrev16(word >> 16);
}

static inline uint64_t bitrev64(uint64_t dword)
{
	return ((uint64_t)bitrev32(dword) << 32) | bitrev32(dword >> 32);
}

/*
 * Note: arbitrary bit-widths can be reversed trivially by rounding
 * up to the nearest power of 2, performing the bit-reverals, and then
 * shifting.
 *
 * E.g. to reverse the 24 least-sigbnificant bits of a 32-bit word:
 *
 * static inline uint32_t bitrev24(uint32_t word) {
 *  return bitrev32(word) >> (32 - 24);
 * }
 */

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_TENSTORRENT_BITREV_H_ */
