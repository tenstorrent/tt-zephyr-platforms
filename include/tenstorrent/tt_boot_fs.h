/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _TT_BOOT_FS_H_
#define _TT_BOOT_FS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TT_BOOT_FS_FD_HEAD_ADDR            (0x0)
/* These defines must change when BOOT_START or DESC_REGION_SIZE change in python toolchain */
#define TT_BOOT_FS_SECURITY_BINARY_FD_ADDR (0x3FE0)
#define TT_BOOT_FS_FAILOVER_HEAD_ADDR      (0x4000)
#define TT_BOOT_FS_IMAGE_TAG_SIZE          8

typedef struct {
	uint32_t image_size: 24;
	uint32_t invalid: 1;
	uint32_t executable: 1;
	uint32_t fd_flags_rsvd: 6;
} fd_flags;

typedef union {
	uint32_t val;
	fd_flags f;
} fd_flags_u;

typedef struct {
	uint32_t signature_size: 12;
	uint32_t sb_phase: 8; /* 0 - Phase0A, 1 - Phase0B */
} security_fd_flags;

typedef union {
	uint32_t val;
	security_fd_flags f;
} security_fd_flags_u;

/* File descriptor */
typedef struct {
	uint32_t spi_addr;
	uint32_t copy_dest;
	fd_flags_u flags;
	uint32_t data_crc;
	security_fd_flags_u security_flags;
	uint8_t image_tag[TT_BOOT_FS_IMAGE_TAG_SIZE];
	uint32_t fd_crc;
} tt_boot_fs_fd;

enum {
	TT_BOOT_FS_OK = 0,
	TT_BOOT_FS_ERR = -1
};

typedef enum {
	TT_BOOT_FS_CHK_OK,
	TT_BOOT_FS_CHK_FAIL,
} tt_checksum_res_t;

uint32_t tt_boot_fs_next(uint32_t prev);

int tt_bootfs_ng_read(const struct device *dev, uint32_t addr, uint8_t *buffer, size_t size);

int tt_bootfs_ng_write(const struct device *dev, uint32_t addr, const uint8_t *buffer, size_t size);

int tt_bootfs_ng_erase(const struct device *dev, uint32_t addr, size_t size);

uint32_t tt_boot_fs_cksum(uint32_t cksum, const uint8_t *data, size_t size);

int tt_boot_fs_ls(const struct device *dev, tt_boot_fs_fd *fds, size_t nfds);

const tt_boot_fs_fd *find_fd_by_tag(const uint8_t *tag, tt_boot_fs_fd *fds, int count);

#ifdef __cplusplus
}
#endif

#endif
