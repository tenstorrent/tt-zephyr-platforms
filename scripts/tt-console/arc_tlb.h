/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef ARC_TLB_H_
#define ARC_TLB_H_

#include <stdint.h>

struct tlb_init_data {
	int verbose;
	uint8_t tlb_id;
	uint16_t pci_device_id;
	const char *dev_name;
};

#define BH_2M_TLB_UC_DYNAMIC_START 190
#define BH_2M_TLB_UC_DYNAMIC_END   199

int tlb_init(void *data);
void tlb_exit(void);
int tlb_write(uint32_t start_addr, const uint8_t *buf, size_t len);
int tlb_read(uint32_t start_addr, uint8_t *buf, size_t len);

#endif /* ARC_TLB_H_ */
