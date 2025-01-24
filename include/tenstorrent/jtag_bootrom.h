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

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *get_bootcode(void);
const size_t get_bootcode_len(void);

int jtag_bootrom_init(void);
int jtag_bootrom_reset(bool force_reset);
int *jtag_bootrom_disable_bus(void);

int jtag_bootrom_setup(void);
int jtag_bootrom_patch_offset(const uint32_t *patch, size_t patch_len, const uint32_t start_addr);
int jtag_bootrom_verify(const uint32_t *patch, size_t patch_len);
void jtag_bootrom_soft_reset_arc(void);
void jtag_bootrom_teardown(void);

ALWAYS_INLINE int jtag_bootrom_patch(const uint32_t *patch, size_t patch_len)
{
	return jtag_bootrom_patch_offset(patch, patch_len, 0);
}

#if IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)
bool jtag_bootrom_needs_reset(void);
void jtag_bootrom_force_reset(void);
struct k_spinlock jtag_bootrom_reset_lock(void);
bool was_arc_reset(void);
void handled_arc_reset(void);
#endif

/* for verification via gpio-emul */
void jtag_bootrom_emul_setup(const uint32_t *buf, size_t buf_len);
int jtag_bootrom_emul_axiread(uint32_t addr, uint32_t *value);

#ifdef __cplusplus
}
#endif

#endif
