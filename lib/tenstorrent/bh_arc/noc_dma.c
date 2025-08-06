/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "noc.h"
#include "noc2axi.h"
#include "util.h"

#define NOC_DMA_TLB        0
#define NOC_DMA_NOC_ID     0
#define NOC_DMA_TIMEOUT_MS 100
#define NOC_MAX_BURST_SIZE 16384

/* NOC CMD fields */
#define NOC_CMD_CPY               (0 << 0)
#define NOC_CMD_RD                (0 << 1)
#define NOC_CMD_WR                (1 << 1)
#define NOC_CMD_RESP_MARKED       (1 << 4)
#define NOC_CMD_BRCST_PACKET      (1 << 5)
#define NOC_CMD_PATH_RESERVE      (1 << 8)
#define NOC_CMD_BRCST_SRC_INCLUDE (1 << 17)

/* NOC0 RISC0 DMA registers */
#define TARGET_ADDR_LO           0xFFB20000
#define TARGET_ADDR_MID          0xFFB20004
#define TARGET_ADDR_HI           0xFFB20008
#define RET_ADDR_LO              0xFFB2000C
#define RET_ADDR_MID             0xFFB20010
#define RET_ADDR_HI              0xFFB20014
#define PACKET_TAG               0xFFB20018
#define CMD_BRCST                0xFFB2001C
#define AT_LEN                   0xFFB20020
#define AT_LEN_1                 0xFFB20024
#define AT_DATA                  0xFFB20028
#define BRCST_EXCLUDE            0xFFB2002C
#define CMD_CTRL                 0xFFB20040
#define NIU_MST_WR_ACK_RECEIVED  0xFFB20204
#define NIU_MST_RD_RESP_RECEIVED 0xFFB20208

struct ret_addr_hi {
	uint32_t end_x: 6;
	uint32_t end_y: 6;
	uint32_t start_x: 6;
	uint32_t start_y: 6;
};

union ret_addr_hi_u {
	struct ret_addr_hi f;
	uint32_t u;
};

static inline void program_noc_dma_tlb(uint8_t x, uint8_t y)
{
	uint32_t addr = TARGET_ADDR_LO;

	NOC2AXITlbSetup(NOC_DMA_NOC_ID, NOC_DMA_TLB, x, y, addr);
}

/* program_noc_dma_tlb must be invoked before this func call */
static inline void write_noc_dma_config(uint32_t addr, uint32_t value)
{
	NOC2AXIWrite32(NOC_DMA_NOC_ID, NOC_DMA_TLB, addr, value);
}

/* program noc_dma_tlb must be invoked before this func call */
static inline uint32_t read_noc_dma_config(uint32_t addr)
{
	return NOC2AXIRead32(NOC_DMA_NOC_ID, NOC_DMA_TLB, addr);
}

static bool noc_wait_cmd_ready(void)
{
	uint32_t cmd_ctrl;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(NOC_DMA_TIMEOUT_MS));

	do {
		cmd_ctrl = read_noc_dma_config(CMD_CTRL);
	} while (cmd_ctrl != 0 && !sys_timepoint_expired(timeout));

	return cmd_ctrl == 0;
}

static uint32_t get_expected_acks(uint32_t noc_cmd, uint64_t size)
{
	uint32_t ack_reg_addr =
		(noc_cmd & NOC_CMD_WR) ? NIU_MST_WR_ACK_RECEIVED : NIU_MST_RD_RESP_RECEIVED;
	uint32_t packet_received = read_noc_dma_config(ack_reg_addr);
	uint32_t expected_acks = packet_received + DIV_ROUND_UP(size, NOC_MAX_BURST_SIZE);

	return expected_acks;
}

/* wrap around aware comparison for half-range rule */
static inline bool is_behind(uint32_t current, uint32_t target)
{
	uint32_t diff = current - target;

	return diff > DIV_ROUND_UP(UINT32_MAX, 2);
}

static bool wait_noc_dma_done(uint32_t noc_cmd, uint32_t expected_acks)
{
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(NOC_DMA_TIMEOUT_MS));
	uint32_t ack_reg_addr =
		(noc_cmd & NOC_CMD_WR) ? NIU_MST_WR_ACK_RECEIVED : NIU_MST_RD_RESP_RECEIVED;
	uint32_t ack_received;
	bool behind;

	do {
		ack_received = read_noc_dma_config(ack_reg_addr);
		behind = is_behind(ack_received, expected_acks);
	} while (behind && !sys_timepoint_expired(timeout));

	return !behind;
}

static uint32_t noc_dma_format_coord(uint8_t x, uint8_t y)
{
	return (union ret_addr_hi_u){.f = {.end_x = x, .end_y = y}}.u;
}

static uint32_t noc_dma_format_multicast(uint8_t start_x, uint8_t start_y, uint8_t end_x,
					 uint8_t end_y)
{
	return (union ret_addr_hi_u){
		.f = {.end_x = end_x, .end_y = end_y, .start_x = start_x, .start_y = start_y}}
		.u;
}

static bool noc_dma_transfer(uint32_t cmd, uint32_t ret_coord, uint64_t ret_addr,
			     uint32_t targ_coord, uint64_t targ_addr, uint32_t size, bool multicast,
			     uint8_t transaction_id, bool include_self, bool wait_for_done)
{
	uint32_t ret_addr_lo = low32(ret_addr);
	uint32_t ret_addr_mid = high32(ret_addr);
	uint32_t ret_addr_hi = ret_coord;

	uint32_t targ_addr_lo = low32(targ_addr);
	uint32_t targ_addr_mid = high32(targ_addr);
	uint32_t targ_addr_hi = targ_coord;

	uint32_t noc_at_len_be = size;
	uint32_t noc_packet_tag = transaction_id << 10;

	uint32_t noc_ctrl = NOC_CMD_CPY | cmd;
	uint32_t expected_acks;

	if (multicast) {
		noc_ctrl |= NOC_CMD_PATH_RESERVE | NOC_CMD_BRCST_PACKET;

		if (include_self) {
			noc_ctrl |= NOC_CMD_BRCST_SRC_INCLUDE;
		}
	}

	if (wait_for_done) {
		noc_ctrl |= NOC_CMD_RESP_MARKED;
		expected_acks = get_expected_acks(noc_ctrl, size);
	}

	if (!noc_wait_cmd_ready()) {
		return false;
	}

	write_noc_dma_config(TARGET_ADDR_LO, targ_addr_lo);
	write_noc_dma_config(TARGET_ADDR_MID, targ_addr_mid);
	write_noc_dma_config(TARGET_ADDR_HI, targ_addr_hi);
	write_noc_dma_config(RET_ADDR_LO, ret_addr_lo);
	write_noc_dma_config(RET_ADDR_MID, ret_addr_mid);
	write_noc_dma_config(RET_ADDR_HI, ret_addr_hi);
	write_noc_dma_config(PACKET_TAG, noc_packet_tag);
	write_noc_dma_config(AT_LEN, noc_at_len_be);
	write_noc_dma_config(AT_LEN_1, 0);
	write_noc_dma_config(AT_DATA, 0);
	write_noc_dma_config(BRCST_EXCLUDE, 0);
	write_noc_dma_config(CMD_BRCST, noc_ctrl);
	write_noc_dma_config(CMD_CTRL, 1);

	if (wait_for_done && !wait_noc_dma_done(noc_ctrl, expected_acks)) {
		return false;
	}

	return true;
}

bool noc_dma_read(uint8_t local_x, uint8_t local_y, uint64_t local_addr, uint8_t remote_x,
		  uint8_t remote_y, uint64_t remote_addr, uint32_t size, bool wait_for_done)
{
	uint32_t ret_coord = noc_dma_format_coord(local_x, local_y);
	uint64_t ret_addr = local_addr;

	uint32_t targ_coord = noc_dma_format_coord(remote_x, remote_y);
	uint64_t targ_addr = remote_addr;

	program_noc_dma_tlb(local_x, local_y);
	return noc_dma_transfer(NOC_CMD_RD, ret_coord, ret_addr, targ_coord, targ_addr, size, false, 0,
				false, wait_for_done);
}

bool noc_dma_write(uint8_t local_x, uint8_t local_y, uint64_t local_addr, uint8_t remote_x,
		   uint8_t remote_y, uint64_t remote_addr, uint32_t size, bool wait_for_done)
{
	uint32_t ret_coord = noc_dma_format_coord(remote_x, remote_y);
	uint64_t ret_addr = remote_addr;

	uint32_t targ_coord = noc_dma_format_coord(local_x, local_y);
	uint64_t targ_addr = local_addr;

	program_noc_dma_tlb(local_x, local_y);
	return noc_dma_transfer(NOC_CMD_WR, ret_coord, ret_addr, targ_coord, targ_addr, size, false, 0,
				false, wait_for_done);
}

static bool noc_dma_write_multicast(uint8_t local_x, uint8_t local_y, uint64_t local_addr,
				    uint8_t remote_start_x, uint8_t remote_start_y,
				    uint8_t remote_end_x, uint8_t remote_end_y,
				    uint64_t remote_addr, uint32_t size, bool include_self)
{
	uint32_t ret_coord = noc_dma_format_multicast(remote_start_x, remote_start_y, remote_end_x,
						      remote_end_y);
	uint64_t ret_addr = remote_addr;

	uint32_t targ_coord = noc_dma_format_coord(local_x, local_y);
	uint64_t targ_addr = local_addr;

	program_noc_dma_tlb(local_x, local_y);
	return noc_dma_transfer(NOC_CMD_WR, ret_coord, ret_addr, targ_coord, targ_addr, size, true,
				0, include_self, false);
}

bool noc_dma_broadcast(uint8_t local_x, uint8_t local_y, uint64_t addr, uint32_t size)
{
	/* Use pre translation coords as NOC translation has enabled. */
	uint8_t remote_start_x = 2;
	uint8_t remote_start_y = 2;
	uint8_t remote_end_x = 1;
	uint8_t remote_end_y = 11;
	uint64_t local_addr = addr;
	uint64_t remote_addr = addr;

	return noc_dma_write_multicast(local_x, local_y, local_addr, remote_start_x, remote_start_y,
				       remote_end_x, remote_end_y, remote_addr, size, false);
}
