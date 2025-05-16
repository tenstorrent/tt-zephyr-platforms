/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef ARC_JTAG_H_
#define ARC_JTAG_H_

#include <stdint.h>

struct jtag_init_data {
	int verbose;
	const char *serial_number;
};

int arc_jtag_init(void *data);
void arc_jtag_exit(void);
int arc_jtag_write_mem(uint32_t start_addr, const uint8_t *buf, size_t len);
int arc_jtag_read_mem(uint32_t start_addr, uint8_t *buf, size_t len);

#endif /* ARC_JTAG_H_ */
