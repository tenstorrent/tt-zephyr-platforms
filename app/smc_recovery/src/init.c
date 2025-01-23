/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "init.h"

#include <stdint.h>
#include <tenstorrent/post_code.h>

#include "cat.h"
#include "init_common.h"
#include "pll.h"
#include "pcie.h"
#include "read_only_table.h"
#include "reg.h"
#include "smbus_target.h"
#include "status_reg.h"

static uint8_t large_sram_buffer[SCRATCHPAD_SIZE] __aligned(4);

/* Returns 0 on success, non-zero on failure */
uint32_t InitHW(void)
{
	/* Write a status register indicating HW init progress */
	STATUS_BOOT_STATUS0_reg_u boot_status0 = {0};

	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitStarted;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP1);

	/* Load Read Only table from SPI filesystem */
	/* TODO: Add some kind of error handling if the load fails */
	load_read_only_table(large_sram_buffer, SCRATCHPAD_SIZE);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP2);
	/* Enable CATMON for early thermal protection */
	CATInit();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP3);
	/* Put all PLLs back into bypass, since tile resets need to be deasserted at low speed */
	PLLAllBypass();
	DeassertTileResets();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP4);
	PLLInit();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP5);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP6);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP7);

	/* Initialize pcie0, pcie1 16 lanes as EP */
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP8);
	FwTable_PciPropertyTable pci_property_table = {
		.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP, .num_serdes = 2};

	if (PCIeInitOk == PCIeInit(0, &pci_property_table)) {
		InitResetInterrupt(0);
	}

	if (PCIeInitOk == PCIeInit(1, &pci_property_table)) {
		InitResetInterrupt(1);
	}

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP9);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPA);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPB);
	InitSmbusTarget();

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPC);

	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEPD);

	/* Indicate successful HW Init */
	boot_status0.val = ReadReg(STATUS_BOOT_STATUS0_REG_ADDR);
	boot_status0.f.hw_init_status = kHwInitDone;
	WriteReg(STATUS_BOOT_STATUS0_REG_ADDR, boot_status0.val);

	return 0;
}
