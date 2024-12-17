/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PCIE_H
#define PCIE_H

#include <stdint.h>
#include "noc2axi.h"
#include "spirom_protobufs/fw_table.pb.h"

typedef enum {
	EndPoint = 0,
	RootComplex = 1,
} PCIeDeviceType;

typedef enum {
	PCIeInitOk = 0,
	PCIeSerdesFWLoadTimeout = 1,
	PCIeLinkTrainTimeout = 2,
} PCIeInitStatus;

#define PCIE_INST0_LOGICAL_X 2
#define PCIE_INST1_LOGICAL_X 11
#define PCIE_LOGICAL_Y       0
#define PCIE_DBI_REG_TLB     14

PCIeInitStatus PCIeInit(uint8_t pcie_inst, const FwTable_PciPropertyTable *pci_prop_table);

static inline void WriteDbiReg(const uint32_t addr, const uint32_t data)
{
	const uint8_t noc_id = 0;

	NOC2AXIWrite32(noc_id, PCIE_DBI_REG_TLB, addr, data);
}

static inline uint32_t ReadDbiReg(const uint32_t addr)
{
	const uint8_t noc_id = 0;

	return NOC2AXIRead32(noc_id, PCIE_DBI_REG_TLB, addr);
}
#endif
