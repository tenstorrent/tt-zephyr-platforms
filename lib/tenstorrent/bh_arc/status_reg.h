/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

/* Scratch registers used for status and error reporting */
#ifndef STATUS_REG_H
#define STATUS_REG_H

#include <stdint.h>

#define RESET_UNIT_SCRATCH_RAM_BASE_ADDR 0x80030400
#define RESET_UNIT_SCRATCH_RAM_REG_ADDR(n)                                                         \
	(RESET_UNIT_SCRATCH_RAM_BASE_ADDR + sizeof(uint32_t) * (n))

#define RESET_UNIT_SCRATCH_BASE_ADDR   0x80030060
#define RESET_UNIT_SCRATCH_REG_ADDR(n) (RESET_UNIT_SCRATCH_BASE_ADDR + sizeof(uint32_t) * (n))

/* SCRATCH_[0-7] */
#define STATUS_POST_CODE_REG_ADDR RESET_UNIT_SCRATCH_REG_ADDR(0)

/* SCRATCH_RAM[0-63] */
#define STATUS_FW_VERSION_REG_ADDR           RESET_UNIT_SCRATCH_RAM_REG_ADDR(0)
/* SCRATCH_RAM_1 is reserved for the security handshake used by bootcode */
#define STATUS_BOOT_STATUS0_REG_ADDR         RESET_UNIT_SCRATCH_RAM_REG_ADDR(2)
#define STATUS_BOOT_STATUS1_REG_ADDR         RESET_UNIT_SCRATCH_RAM_REG_ADDR(3)
#define STATUS_ERROR_STATUS0_REG_ADDR        RESET_UNIT_SCRATCH_RAM_REG_ADDR(4)
#define STATUS_ERROR_STATUS1_REG_ADDR        RESET_UNIT_SCRATCH_RAM_REG_ADDR(5)
#define STATUS_INTERFACE_TABLE_BASE_REG_ADDR RESET_UNIT_SCRATCH_RAM_REG_ADDR(6)
/* SCRATCH_RAM_7 is reserved for possible future interface table uses */
#define STATUS_MSG_Q_STATUS_REG_ADDR         RESET_UNIT_SCRATCH_RAM_REG_ADDR(8)
#define STATUS_MSG_Q_ERR_FLAGS_REG_ADDR      RESET_UNIT_SCRATCH_RAM_REG_ADDR(9)
#define STATUS_GDDR_AXI_EN_FLAGS_REG_ADDR    RESET_UNIT_SCRATCH_RAM_REG_ADDR(10)
#define STATUS_MSG_Q_INFO_REG_ADDR           RESET_UNIT_SCRATCH_RAM_REG_ADDR(11)
#define STATUS_FW_VUART_REG_ADDR(n)          RESET_UNIT_SCRATCH_RAM_REG_ADDR(40 + (n))
/* SCRATCH_RAM_40 - SCRATCH_RAM_41 reserved for virtual uarts */
#define STATUS_FW_SCRATCH_REG_ADDR           RESET_UNIT_SCRATCH_RAM_REG_ADDR(63)

typedef struct {
	uint32_t msg_queue_ready: 1;
	uint32_t hw_init_status: 2;
	uint32_t fw_id: 4;
	uint32_t spare: 25;
} STATUS_BOOT_STATUS0_reg_t;

typedef union {
	uint32_t val;
	STATUS_BOOT_STATUS0_reg_t f;
} STATUS_BOOT_STATUS0_reg_u;
#endif
