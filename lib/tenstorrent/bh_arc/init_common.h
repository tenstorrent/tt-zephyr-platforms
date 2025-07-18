/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INIT_COMMON_H
#define INIT_COMMON_H

#include <stdint.h>

#define RESET_UNIT_GLOBAL_RESET_REG_ADDR 0x80030000
#define RESET_UNIT_ETH_RESET_REG_ADDR    0x80030008
#define RESET_UNIT_DDR_RESET_REG_ADDR    0x80030010
#define RESET_UNIT_L2CPU_RESET_REG_ADDR  0x80030014

#define RESET_UNIT_TENSIX_RESET_0_REG_ADDR 0x80030020
#define RESET_UNIT_TENSIX_RESET_1_REG_ADDR 0x80030024
#define RESET_UNIT_TENSIX_RESET_2_REG_ADDR 0x80030028
#define RESET_UNIT_TENSIX_RESET_3_REG_ADDR 0x8003002C
#define RESET_UNIT_TENSIX_RESET_4_REG_ADDR 0x80030030
#define RESET_UNIT_TENSIX_RESET_5_REG_ADDR 0x80030034
#define RESET_UNIT_TENSIX_RESET_6_REG_ADDR 0x80030038
#define RESET_UNIT_TENSIX_RESET_7_REG_ADDR 0x8003003C

#define RESET_UNIT_TENSIX_RISC_RESET_0_REG_ADDR 0x80030040
#define SCRATCHPAD_SIZE                         CONFIG_TT_BH_ARC_SCRATCHPAD_SIZE

typedef struct {
	uint32_t system_reset_n: 1;
	uint32_t noc_reset_n: 1;
	uint32_t rsvd_0: 5;
	uint32_t refclk_cnt_en: 1;
	uint32_t pcie_reset_n: 2;
	uint32_t rsvd_1: 3;
	uint32_t ptp_reset_n_refclk: 1;
} RESET_UNIT_GLOBAL_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_GLOBAL_RESET_reg_t f;
} RESET_UNIT_GLOBAL_RESET_reg_u;

#define RESET_UNIT_GLOBAL_RESET_REG_DEFAULT (0x00000080)

typedef struct {
	uint32_t eth_reset_n: 14;
	uint32_t rsvd_0: 2;
	uint32_t eth_risc_reset_n: 14;
} RESET_UNIT_ETH_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_ETH_RESET_reg_t f;
} RESET_UNIT_ETH_RESET_reg_u;

#define RESET_UNIT_ETH_RESET_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t tensix_reset_n: 32;
} RESET_UNIT_TENSIX_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_TENSIX_RESET_reg_t f;
} RESET_UNIT_TENSIX_RESET_reg_u;

#define RESET_UNIT_TENSIX_RESET_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t ddr_reset_n: 8;
	uint32_t ddr_risc_reset_n: 24;
} RESET_UNIT_DDR_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_DDR_RESET_reg_t f;
} RESET_UNIT_DDR_RESET_reg_u;

#define RESET_UNIT_DDR_RESET_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t l2cpu_reset_n: 4;
	uint32_t l2cpu_risc_reset_n: 4;
} RESET_UNIT_L2CPU_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_L2CPU_RESET_reg_t f;
} RESET_UNIT_L2CPU_RESET_reg_u;

#define RESET_UNIT_L2CPU_RESET_REG_DEFAULT (0x00000000)

typedef enum {
	kHwInitNotStarted = 0,
	kHwInitStarted = 1,
	kHwInitDone = 2,
	kHwInitError = 3,
} HWInitStatus;

typedef enum {
	FW_ID_SMC_NORMAL = 0,
	FW_ID_SMC_RECOVERY = 1,
} FWID;

int SpiReadWrap(uint32_t addr, uint32_t size, uint8_t *dst);
void InitResetInterrupt(uint8_t pcie_inst);

#endif
