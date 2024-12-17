/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spi_controller.h"

#include <limits.h>
#include <zephyr/sys/util.h>

#include "pll.h"
#include "reg.h"
#include "timer.h"

#define SPICLK_FREQ_MHZ 40

typedef struct {
	uint32_t trans_type: 2;
	uint32_t addr_l: 4;
	uint32_t rsvd_spi_ctrlr0_6_7: 2;
	uint32_t inst_l: 2;
	uint32_t rsvd_spi_ctrlr0_10: 1;
	uint32_t wait_cycles: 5;
	uint32_t spi_ddr_en: 1;
	uint32_t inst_ddr_en: 1;
	uint32_t spi_rxds_en: 1;
	uint32_t rsvd_spi_ctrlr0: 13;
} DW_APB_SSI_SPI_CTRLR0_reg_t;

typedef union {
	uint32_t val;
	DW_APB_SSI_SPI_CTRLR0_reg_t f;
} DW_APB_SSI_SPI_CTRLR0_reg_u;

#define DW_APB_SSI_SPI_CTRLR0_REG_DEFAULT 0x00000200

typedef struct {
	uint32_t busy: 1;
	uint32_t tfnf: 1;
	uint32_t tfe: 1;
	uint32_t rfne: 1;
	uint32_t rff: 1;
	uint32_t rsvd_txe: 1;
	uint32_t dcol: 1;
	uint32_t rsvd_sr: 25;
} DW_APB_SSI_SR_reg_t;

typedef union {
	uint32_t val;
	DW_APB_SSI_SR_reg_t f;
} DW_APB_SSI_SR_reg_u;

#define DW_APB_SSI_SR_REG_DEFAULT 0x00000006

typedef struct {
	uint32_t dfs: 4;
	uint32_t frf: 2;
	uint32_t scph: 1;
	uint32_t scpol: 1;
	uint32_t tmod: 2;
	uint32_t slv_oe: 1;
	uint32_t srl: 1;
	uint32_t cfs: 4;
	uint32_t dfs_32: 5;
	uint32_t spi_frf: 2;
	uint32_t rsvd_ctrlr0_23: 1;
	uint32_t sste: 1;
	uint32_t seconv: 1;
	uint32_t rsvd_ctrlr0: 6;
} DW_APB_SSI_CTRLR0_reg_t;

typedef union {
	uint32_t val;
	DW_APB_SSI_CTRLR0_reg_t f;
} DW_APB_SSI_CTRLR0_reg_u;

#define DW_APB_SSI_CTRLR0_REG_DEFAULT 0x03070000

typedef struct {
	uint32_t boot_spi_mode: 2;
	uint32_t boot_ddr: 1;
	uint32_t boot_dqs: 1;
	uint32_t boot_address_mode: 4;
	uint32_t normal_spi_mode: 2;
	uint32_t normal_ddr: 1;
	uint32_t normal_dqs: 1;
	uint32_t normal_address_mode: 4;
	uint32_t device_addr_bytes: 4;
	uint32_t flash_family: 2;
} RESET_UNIT_SPI_DEVICE_CONFIG_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_SPI_DEVICE_CONFIG_reg_t f;
} RESET_UNIT_SPI_DEVICE_CONFIG_reg_u;

#define RESET_UNIT_SPI_DEVICE_CONFIG_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t en: 1;
	uint32_t ddr_en: 1;
	uint32_t rsvd_0: 2;
	uint32_t reset: 1;
	uint32_t rsvd_1: 3;
	uint32_t pclk_disable: 1;
	uint32_t refclk_disable: 1;
	uint32_t rsvd_2: 6;
	uint32_t tx_ack: 1;
	uint32_t rx_ack: 1;
	uint32_t rsvd_3: 6;
	uint32_t dqs_delay_cycles: 3;
} RESET_UNIT_SPI_CNTL_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_SPI_CNTL_reg_t f;
} RESET_UNIT_SPI_CNTL_reg_u;

#define RESET_UNIT_SPI_CNTL_REG_DEFAULT (0x03000000)

#define DW_APB_SSI_SSIENR_REG_ADDR            0x80070008
#define DW_APB_SSI_SPI_CTRLR0_REG_ADDR        0x800700F4
#define DW_APB_SSI_SR_REG_ADDR                0x80070028
#define DW_APB_SSI_DR0_REG_ADDR               0x80070060
#define DW_APB_SSI_SSIENR_REG_ADDR            0x80070008
#define DW_APB_SSI_RX_SAMPLE_DLY_REG_ADDR     0x800700F0
#define DW_APB_SSI_SPI_CTRLR0_REG_ADDR        0x800700F4
#define DW_APB_SSI_CTRLR0_REG_ADDR            0x80070000
#define DW_APB_SSI_BAUDR_REG_ADDR             0x80070014
#define DW_APB_SSI_TXD_DRIVE_EDGE_REG_ADDR    0x800700F8
#define RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR 0x800300D4
#define DW_APB_SSI_SER_REG_ADDR               0x80070010
#define DW_APB_SSI_CTRLR1_REG_ADDR            0x80070004
#define RESET_UNIT_SPI_CNTL_REG_ADDR          0x800300F8

typedef enum {
	TmodTxOnly = 1,
	TmodEepromRead = 3,
} TransMode;

typedef enum {
	InstStandardAddrSpiFrf = 1,
	InstAddrSpiFrf = 2,
} TransType;

void SpiProgramWaitCyclesAndAddrl(uint8_t wait_cycles, uint8_t addr_width)
{
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	DW_APB_SSI_SPI_CTRLR0_reg_u spi_ctrlr0;

	spi_ctrlr0.val = ReadReg(DW_APB_SSI_SPI_CTRLR0_REG_ADDR);
	spi_ctrlr0.f.addr_l = addr_width * 2;
	spi_ctrlr0.f.wait_cycles = wait_cycles;
	WriteReg(DW_APB_SSI_SPI_CTRLR0_REG_ADDR, spi_ctrlr0.val);
}

static void WaitTxFifoEmpty(void)
{
	DW_APB_SSI_SR_reg_u sr;

	do {
		sr.val = ReadReg(DW_APB_SSI_SR_REG_ADDR);
	} while (sr.f.tfe == 0);
}

static void WaitTransactionDone(void)
{
	DW_APB_SSI_SR_reg_u sr;

	do {
		sr.val = ReadReg(DW_APB_SSI_SR_REG_ADDR);
	} while (sr.f.busy == 1);
}

static void WaitRxFifoNotEmpty(void)
{
	DW_APB_SSI_SR_reg_u sr;

	do {
		sr.val = ReadReg(DW_APB_SSI_SR_REG_ADDR);
	} while (sr.f.rfne == 0);
}

static void PushTxFifo(uint32_t *data_array, uint32_t num_frames)
{
	for (uint32_t i = 0; i < num_frames; i++) {
		WriteReg(DW_APB_SSI_DR0_REG_ADDR, data_array[i]);
	}
}

static void PopRxFifo(uint8_t *data_array, uint32_t num_frames)
{
	for (uint32_t i = 0; i < num_frames; i++) {
		WaitRxFifoNotEmpty();
		data_array[i] = ReadReg(DW_APB_SSI_DR0_REG_ADDR);
	}
}

void SetRxSampleDelay(uint32_t delay)
{
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	WriteReg(DW_APB_SSI_RX_SAMPLE_DLY_REG_ADDR, delay);
}

void SpiControllerModeSetup(bool ddr, SpiIoMode io_mode)
{
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	DW_APB_SSI_CTRLR0_reg_u ctrlr0;

	ctrlr0.val = ReadReg(DW_APB_SSI_CTRLR0_REG_ADDR);
	ctrlr0.f.spi_frf = io_mode;
	ctrlr0.f.scph = 0;
	ctrlr0.f.sste = 0;
	ctrlr0.f.dfs_32 = CHAR_BIT - 1;
	WriteReg(DW_APB_SSI_CTRLR0_REG_ADDR, ctrlr0.val);

	if (io_mode != SpiStandardMode) {
		DW_APB_SSI_SPI_CTRLR0_reg_u spi_ctrlr0;

		spi_ctrlr0.val = ReadReg(DW_APB_SSI_SPI_CTRLR0_REG_ADDR);
		spi_ctrlr0.f.spi_rxds_en = 0;
		spi_ctrlr0.f.inst_ddr_en = io_mode == SpiOctalMode ? 0 : ddr;
		spi_ctrlr0.f.spi_ddr_en = ddr;
		spi_ctrlr0.f.trans_type = InstAddrSpiFrf;
		WriteReg(DW_APB_SSI_SPI_CTRLR0_REG_ADDR, spi_ctrlr0.val);
	}
}

void SpiControllerClkSetup(bool ddr)
{
	/* round up clock_div to nearest even value since dw_apb_ssi sets LSB to 0 */
	uint16_t clock_div = DIV_ROUND_UP(GetARCCLK(), SPICLK_FREQ_MHZ) + 1;

	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	WriteReg(DW_APB_SSI_BAUDR_REG_ADDR, clock_div);

	if (ddr) {
		WriteReg(DW_APB_SSI_TXD_DRIVE_EDGE_REG_ADDR, clock_div / 4);
	}
}

void SpiDetectOpMode(bool *ddr, SpiIoMode *io_mode, uint8_t *addr_width)
{
	RESET_UNIT_SPI_DEVICE_CONFIG_reg_u spi_device_config;

	spi_device_config.val = ReadReg(RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR);
	*ddr = spi_device_config.f.normal_ddr;
	*io_mode = spi_device_config.f.normal_spi_mode;

	/* This is to workaround a bug in A0 bootrom, normal address mode is not populated for MT25
	 */
	if (spi_device_config.f.normal_address_mode == 0) {
		*addr_width = spi_device_config.f.boot_address_mode;
	} else {
		*addr_width = spi_device_config.f.normal_address_mode;
	}
}

void EepromTransmit(uint32_t *tx_data, uint32_t num_frames)
{
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	DW_APB_SSI_CTRLR0_reg_u ctrlr0;

	ctrlr0.val = ReadReg(DW_APB_SSI_CTRLR0_REG_ADDR);
	ctrlr0.f.tmod = TmodTxOnly;
	WriteReg(DW_APB_SSI_CTRLR0_REG_ADDR, ctrlr0.val);
	WriteReg(DW_APB_SSI_SER_REG_ADDR, 0);
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 1);
	PushTxFifo(tx_data, num_frames);
	WriteReg(DW_APB_SSI_SER_REG_ADDR, 1);
	WaitTxFifoEmpty();
	WaitTransactionDone();
}

void EepromRead(uint32_t *tx_data, uint32_t num_write_frames, uint8_t *rx_data,
		uint32_t num_read_frames)
{
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 0);
	DW_APB_SSI_CTRLR0_reg_u ctrlr0;

	ctrlr0.val = ReadReg(DW_APB_SSI_CTRLR0_REG_ADDR);
	ctrlr0.f.tmod = TmodEepromRead;
	WriteReg(DW_APB_SSI_CTRLR0_REG_ADDR, ctrlr0.val);
	WriteReg(DW_APB_SSI_SER_REG_ADDR, 0);
	WriteReg(DW_APB_SSI_CTRLR1_REG_ADDR, num_read_frames - 1);
	WriteReg(DW_APB_SSI_SSIENR_REG_ADDR, 1);
	PushTxFifo(tx_data, num_write_frames);
	WriteReg(DW_APB_SSI_SER_REG_ADDR, 1);
	PopRxFifo(rx_data, num_read_frames);
}

void SpiControllerReset(void)
{
	RESET_UNIT_SPI_CNTL_reg_u spi_cntl;

	spi_cntl.val = ReadReg(RESET_UNIT_SPI_CNTL_REG_ADDR);
	spi_cntl.f.reset = 1;
	WriteReg(RESET_UNIT_SPI_CNTL_REG_ADDR, spi_cntl.val);
	WaitUs(1);
	spi_cntl.f.reset = 0;
	WriteReg(RESET_UNIT_SPI_CNTL_REG_ADDR, spi_cntl.val);
}
