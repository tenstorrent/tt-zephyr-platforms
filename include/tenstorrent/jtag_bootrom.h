/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_JTAG_BOOTROM_H_
#define TENSTORRENT_JTAG_BOOTROM_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/util.h>
#include <zephyr/spinlock.h>

#include <tenstorrent/bh_chip.h>

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *get_bootcode(void);
const size_t get_bootcode_len(void);

int jtag_bootrom_init(struct bh_chip *chip);

int jtag_bootrom_reset_asic(struct bh_chip *chip);

int jtag_bootrom_patch_offset(struct bh_chip *chip, const uint32_t *patch, size_t patch_len,
			      const uint32_t start_addr);
int jtag_bootrom_verify(const struct device *dev, const uint32_t *patch, size_t patch_len);
void jtag_bootrom_soft_reset_arc(struct bh_chip *chip);
void jtag_bootrom_teardown(const struct bh_chip *chip);

ALWAYS_INLINE int jtag_bootrom_patch(struct bh_chip *chip, const uint32_t *patch, size_t patch_len)
{
	return jtag_bootrom_patch_offset(chip, patch, patch_len, 0);
}

/* for verification via gpio-emul */
void jtag_bootrom_emul_setup(const uint32_t *buf, size_t buf_len);
int jtag_bootrom_emul_axiread(uint32_t addr, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif
