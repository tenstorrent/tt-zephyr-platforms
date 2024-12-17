/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "timer.h"
#include "dw_apb_i2c.h"
#include "reg.h"
#include "efuse.h"

#define EFUSE_DFT0_MEM_BASE_ADDR          0x80040000
#define EFUSE_DFT0_CNTL_REG_MAP_BASE_ADDR 0x80048000

/* Efuse Power switch I2C constants */
#define EFUSE_POWER_SWITCH0_ADDR 0x72
#define EFUSE_POWER_SWITCH1_ADDR 0x73
#define EFUSE_CTRL_REG_ADDR      0x5
#define EFUSE_I2C_MST_ID         2
#define EFUSE_CMD_BYTE_SIZE      1
#define EFUSE_DATA_BYTE_SIZE     1
#define VQPS_HI                  1
#define VQPS_LO                  0

#define EFUSE_BOX_ADDR_ALIGN             0x2000
#define EFUSE_SECURITY_BOX_MEM_BASE_ADDR 0xB0040000
#define EFUSE_SECURITY_REG_OFFSET_ADDR   0x8000
#define EFUSE_BOX_START_ADDR(box_id)     \
	(EFUSE_DFT0_MEM_BASE_ADDR + (box_id) * EFUSE_BOX_ADDR_ALIGN)
#define EFUSE_CTRL_REG_START_ADDR(box_id)                                                          \
	(EFUSE_DFT0_CNTL_REG_MAP_BASE_ADDR + (box_id) * EFUSE_BOX_ADDR_ALIGN)

#define EFUSE_ROW_SIZE             32
#define EFUSE_BOX_SIZE_BITS        8192
#define EFUSE_RD_CNTL_REG_OFFSET   (0x0)
#define EFUSE_MISC_CNTL_REG_OFFSET (0x8)
#define EFUSE_DATA_REG_OFFSET      (0xC)
#define GET_EFUSE_CNTL_ADDR(box_id, reg_name)                                                      \
	(EFUSE_##reg_name##_REG_OFFSET + EFUSE_CTRL_REG_START_ADDR(box_id))

typedef struct {
	uint32_t csb: 1;
	uint32_t load: 1;
	uint32_t rsvd_0: 6;
	uint32_t strobe: 1;
	uint32_t rsvd_1: 7;
	uint32_t addr: 13;
	uint32_t rsvd_2: 2;
	uint32_t ovrd: 1;
} EFUSE_CNTL_EFUSE_RD_CNTL_reg_t;

typedef union {
	uint32_t val;
	EFUSE_CNTL_EFUSE_RD_CNTL_reg_t f;
} EFUSE_CNTL_EFUSE_RD_CNTL_reg_u;

#define EFUSE_CNTL_EFUSE_RD_CNTL_REG_DEFAULT (0x00000001)

/* TODO: need to adjust address for securiy efuse */
/* Read Efuse at EFUSE_BOX_START_ADDR + offset, the offset needs to be 32-bit aligned.  */
uint32_t EfuseRead(EfuseAccessType acc_type, EfuseBoxId efuse_box_id, uint32_t offset)
{
	if (acc_type == EfuseDirect) {
		uint32_t volatile *p_efuse =
			(uint32_t volatile *)EFUSE_BOX_START_ADDR(efuse_box_id);
		return p_efuse[offset];
	}

	EFUSE_CNTL_EFUSE_RD_CNTL_reg_u efuse_rd_cntl_reg;

	efuse_rd_cntl_reg.val = EFUSE_CNTL_EFUSE_RD_CNTL_REG_DEFAULT;
	efuse_rd_cntl_reg.f.csb = 0;       /* set chip select, active low */
	efuse_rd_cntl_reg.f.load = 1;      /* set fuse sense enable, active high */
	efuse_rd_cntl_reg.f.addr = offset; /* set 32-bit aligned address */
	efuse_rd_cntl_reg.f.ovrd = 1;      /* take control over the bus */

	WriteReg(GET_EFUSE_CNTL_ADDR(efuse_box_id, RD_CNTL), efuse_rd_cntl_reg.val);
	WaitNs(60); /* wait > 30ns to cover setup time */

	/* Toggle strobe */
	efuse_rd_cntl_reg.f.strobe = 1;
	WriteReg(GET_EFUSE_CNTL_ADDR(efuse_box_id, RD_CNTL), efuse_rd_cntl_reg.val);
	WaitNs(80);
	efuse_rd_cntl_reg.f.strobe = 0;
	WriteReg(GET_EFUSE_CNTL_ADDR(efuse_box_id, RD_CNTL), efuse_rd_cntl_reg.val);
	WaitNs(60);

	/* Release indirect access registers */
	efuse_rd_cntl_reg.f.ovrd = 0;
	WriteReg(GET_EFUSE_CNTL_ADDR(efuse_box_id, RD_CNTL), efuse_rd_cntl_reg.val);

	return ReadReg(GET_EFUSE_CNTL_ADDR(efuse_box_id, DATA));
}
