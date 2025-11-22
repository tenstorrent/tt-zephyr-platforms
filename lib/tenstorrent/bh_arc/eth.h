/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ETH_H
#define ETH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX_ETH_INSTANCES 14

void SetupEthSerdesMux(uint32_t eth_enabled);
int LoadEthFw(uint32_t eth_inst, uint32_t ring, uint8_t *buf, size_t buf_size, size_t spi_address,
	      size_t image_size);
int LoadEthFwCfg(uint32_t eth_inst, uint32_t ring, uint8_t *buf, uint32_t eth_enabled,
		 size_t spi_address, size_t image_size);
void ReleaseEthReset(uint32_t eth_inst, uint32_t ring);

#endif
