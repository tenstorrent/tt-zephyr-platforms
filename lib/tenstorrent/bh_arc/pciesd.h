/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>

#include "arc_dma.h"
#include "pcie.h"

/* Verify prototype of ArcDmaTransfer, because it's used by libpciesd.a. */
__unused static bool (*verify_ArcDmaTransfer)(const void *, void *, uint32_t) = ArcDmaTransfer;

/* The functions below are implemented in tt_blackhole_libpciesd.a */
PCIeInitStatus SerdesInit(uint8_t pcie_inst, PCIeDeviceType device_type,
			  uint8_t num_serdes_instance);
void ExitLoopback(void);
void EnterLoopback(void);
void CntlInit(uint8_t pcie_inst, uint8_t num_serdes_instance, uint8_t max_pcie_speed,
	      uint64_t board_id, uint32_t vendor_id);
