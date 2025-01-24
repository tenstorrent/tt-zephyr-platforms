/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spi_eeprom.h"
#include "status_reg.h"

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>

#include "reg.h"
#include "spi_controller.h"
#include "util.h"

#define SPI_PAGE_SIZE   256
#define SECTOR_SIZE     4096
#define SPI_BUFFER_SIZE 4096
#define BYTE_GET(v, b)  FIELD_GET(0xFFu << ((b) * 8), (v))

#define SSI_RX_DLY_SR_DEPTH            64
#define SPI_RX_SAMPLE_DELAY_TRAIN_ADDR 0x13FFC
#define SPI_RX_SAMPLE_DELAY_TRAIN_DATA 0xA5A55A5A

static bool reinit_spiclk;
static uint8_t spi_global_buffer[SPI_BUFFER_SIZE];

typedef struct {
	uint8_t code;
	uint8_t wait_cycles;
} Command;

typedef struct {
	Command read_cmd;
	Command program_cmd;
	Command erase_cmd;
	Command write_en_cmd;
	Command read_status_cmd;
	Command enter_4byte_addr_mode_cmd;
	bool ddr;
	SpiIoMode io_mode;
	uint8_t addr_width;
} Eeprom;

/* SPI command definitions */
#define SPI_READ                               0x3
#define MT35_OCTAL_IO_FAST_READ                0xCB
#define MT35_DDR_OCTAL_IO_FAST_READ            0xFD
#define SPI_PAGE_PROGRAM                       0x2
#define MT35_EXTENDED_INPUT_OCTAL_FAST_PROGRAM 0xC2
#define SPI_4KB_SECTOR_ERASE                   0x20
#define SPI_READ_STATUS_REG                    0x5
#define SPI_WRITE_ENABLE                       0x6
#define SPI_ENTER_4BYTE_ADDR_MODE              0xB7
#define MT25_QUAD_INPUT_OUTPUT_FAST_READ       0xEB
#define MT25_DTR_QUAD_INPUT_OUTPUT_FAST_READ   0xED
#define MT25_QUAD_INPUT_EXTENDED_FAST_PROGRAM  0x3E

static Eeprom eeprom = {
	.read_cmd = {SPI_READ},
	.program_cmd = {SPI_PAGE_PROGRAM},
	.erase_cmd = {SPI_4KB_SECTOR_ERASE},
	.write_en_cmd = {SPI_WRITE_ENABLE},
	.read_status_cmd = {SPI_READ_STATUS_REG},
	.enter_4byte_addr_mode_cmd = {SPI_ENTER_4BYTE_ADDR_MODE},
	.ddr = false,
	.io_mode = SpiStandardMode,
	.addr_width = 4,
};

static void SpiUpdateOpMode(bool ddr, SpiIoMode io_mode, uint8_t addr_width)
{
	eeprom.ddr = ddr;
	eeprom.io_mode = io_mode;
	eeprom.addr_width = addr_width;
}

void SpiBufferSetup(void)
{
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(10),
		 ((uint32_t)LOG2(SPI_BUFFER_SIZE) << 24) |
			 ((uint32_t)spi_global_buffer & 0xFFFFFF));
}

void ReinitSpiclk(void)
{
	reinit_spiclk = true;
}

static void PackTx(uint8_t command, uint32_t *paddress, uint8_t *data, uint32_t data_len,
		   uint32_t *tx_data)
{
	uint32_t tx_index = 0;

	tx_data[tx_index++] = command;
	if (paddress != NULL) {
		uint32_t address = *paddress;

		if (eeprom.io_mode == SpiStandardMode) {
			tx_data[tx_index++] = BYTE_GET(address, 3);
			tx_data[tx_index++] = BYTE_GET(address, 2);
			tx_data[tx_index++] = BYTE_GET(address, 1);
			tx_data[tx_index++] = BYTE_GET(address, 0);
		} else {
			tx_data[tx_index++] = address;
		}
	}

	for (uint32_t i = 0; i < data_len; i++) {
		tx_data[tx_index++] = data[i];
	}
}

static void enter_four_byte_address_mode(void)
{
	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.enter_4byte_addr_mode_cmd.wait_cycles, 0);
	}

	uint32_t tx_data[1];

	PackTx(eeprom.enter_4byte_addr_mode_cmd.code, NULL, NULL, 0, tx_data);
	EepromTransmit(tx_data, ARRAY_SIZE(tx_data));
	eeprom.addr_width = 4;
}

static uint8_t SpiReadStatus(void)
{
	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.read_status_cmd.wait_cycles, 0);
	}

	uint32_t tx_data[1];

	PackTx(eeprom.read_status_cmd.code, NULL, NULL, 0, tx_data);

	uint8_t rx_data[1];

	EepromRead(tx_data, ARRAY_SIZE(tx_data), rx_data, ARRAY_SIZE(rx_data));
	return rx_data[0];
}

static void SpiSingleRead(uint32_t address, uint32_t num_frames, uint8_t *rx_data)
{
	uint32_t num_write_frames = 5;

	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.read_cmd.wait_cycles, eeprom.addr_width);
		num_write_frames = 2;
	}

	uint32_t tx_data[num_write_frames];

	PackTx(eeprom.read_cmd.code, &address, NULL, 0, tx_data);
	EepromRead(tx_data, ARRAY_SIZE(tx_data), rx_data, num_frames);
}

static void CalibrateRxSampleDelay(void)
{
	int32_t first_pass = -1;
	int32_t last_pass = -1;

	for (uint32_t i = 0; i < SSI_RX_DLY_SR_DEPTH; i++) {
		SetRxSampleDelay(i);
		uint32_t data;

		SpiSingleRead(SPI_RX_SAMPLE_DELAY_TRAIN_ADDR, sizeof(data), (uint8_t *)&data);
		if (data == SPI_RX_SAMPLE_DELAY_TRAIN_DATA) {
			if (first_pass == -1) {
				first_pass = i;
			}
			last_pass = i;
		} else if (last_pass != -1) {
			break;
		}
	}

	uint32_t optimal_point = 0;

	if (first_pass != -1) {
		optimal_point = (first_pass + last_pass) / 2;
	}

	SetRxSampleDelay(optimal_point);
}

void EepromSetup(void)
{
	bool ddr;
	SpiIoMode io_mode;
	uint8_t addr_width;

	SpiDetectOpMode(&ddr, &io_mode, &addr_width);
	SpiUpdateOpMode(ddr, io_mode, addr_width);

	SpiControllerModeSetup(ddr, io_mode);
	SpiControllerClkSetup(ddr);

	if (eeprom.io_mode == SpiQuadMode) {
		if (eeprom.ddr) {
			eeprom.read_cmd.code = MT25_DTR_QUAD_INPUT_OUTPUT_FAST_READ;
			eeprom.read_cmd.wait_cycles = 8;

			/* These dummy cycles do not match with MT25 spec, needed only for QUAD DDR
			 * as the first 4b read was incorrect.
			 */
			eeprom.read_status_cmd.wait_cycles = 8;
		} else {
			eeprom.read_cmd.code = MT25_QUAD_INPUT_OUTPUT_FAST_READ;
			eeprom.read_cmd.wait_cycles = 10;
		}
		eeprom.program_cmd.code = MT25_QUAD_INPUT_EXTENDED_FAST_PROGRAM;
	} else if (eeprom.io_mode == SpiOctalMode) {
		if (eeprom.ddr) {
			eeprom.read_cmd.code = MT35_DDR_OCTAL_IO_FAST_READ;
			eeprom.read_cmd.wait_cycles = 16;
		} else {
			eeprom.read_cmd.code = MT35_OCTAL_IO_FAST_READ;
			eeprom.read_cmd.wait_cycles = 16;
		}
		eeprom.program_cmd.code = MT35_EXTENDED_INPUT_OCTAL_FAST_PROGRAM;
		eeprom.read_status_cmd.wait_cycles = 8;
	}

	if (eeprom.addr_width != 4) {
		enter_four_byte_address_mode();
	}
	CalibrateRxSampleDelay();
}

static void EepromClkSetup(void)
{
	if (reinit_spiclk) {
		reinit_spiclk = false;
		SpiControllerClkSetup(eeprom.ddr);
		CalibrateRxSampleDelay();
	}
}

void SpiBlockRead(uint32_t spi_address, uint32_t num_bytes, uint8_t *dest)
{
	EepromClkSetup();

	uint32_t bytes_read = 0;

	while (bytes_read < num_bytes) {
		uint32_t curr_read_bytes = 0;

		curr_read_bytes = MIN(SPI_RX_FIFO_DEPTH, num_bytes - bytes_read);
		SpiSingleRead(spi_address + bytes_read, curr_read_bytes, dest + bytes_read);
		bytes_read += curr_read_bytes;
	}
}

static void SpiWaitReady(void)
{
	uint8_t busy;

	do {
		busy = SpiReadStatus() & 1;
	} while (busy);
}

static void SpiWriteEnable(void)
{
	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.write_en_cmd.wait_cycles, 0);
	}

	uint32_t tx_data[1];

	PackTx(eeprom.write_en_cmd.code, NULL, NULL, 0, tx_data);
	EepromTransmit(tx_data, ARRAY_SIZE(tx_data));
}

static void SpiEraseSector(uint32_t address)
{
	SpiWriteEnable();

	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.erase_cmd.wait_cycles, 0);
	}

	uint32_t tx_data[eeprom.addr_width + 1];

	FlipBytes((uint8_t *)&address, eeprom.addr_width);
	PackTx(eeprom.erase_cmd.code, NULL, (uint8_t *)&address, eeprom.addr_width, tx_data);
	EepromTransmit(tx_data, ARRAY_SIZE(tx_data));
	SpiWaitReady();
}

static void AlignProgramAddrData(uint32_t *paddress, const uint8_t *data, uint32_t *num_frames,
				 uint8_t *adjusted_data)
{
	uint8_t *adjusted_data_start = adjusted_data;
	uint32_t frames_to_copy = *num_frames;

	if (eeprom.ddr) {
		if (*paddress % 2 != 0) {
			*paddress -= 1;
			adjusted_data[0] = 0xff;
			adjusted_data_start += 1;
			*num_frames += 1;
		}

		if (*num_frames % 2 != 0) {
			adjusted_data[*num_frames] = 0xff;
			*num_frames += 1;
		}
	}
	memcpy(adjusted_data_start, data, frames_to_copy);
}

static void SpiSingleProgram(uint32_t address, const uint8_t *data, uint32_t num_frames)
{
	uint8_t adjusted_data[num_frames + 2];

	AlignProgramAddrData(&address, data, &num_frames, adjusted_data);
	SpiWriteEnable();

	uint32_t num_write_frames = num_frames + 5;

	if (eeprom.io_mode != SpiStandardMode) {
		SpiProgramWaitCyclesAndAddrl(eeprom.program_cmd.wait_cycles, eeprom.addr_width);
		num_write_frames = num_frames + 2;
	}

	uint32_t tx_data[num_write_frames];

	PackTx(eeprom.program_cmd.code, &address, adjusted_data, num_frames, tx_data);
	EepromTransmit(tx_data, ARRAY_SIZE(tx_data));
	SpiWaitReady();
}

/* align operation region to chunk boundary */
/* offset is offset into start_addr, num_bytes is total bytes of operation */
static inline uint32_t NextChunkSize(const uint32_t start_addr, const uint32_t offset,
				     const uint32_t num_bytes, const uint32_t size)
{
	uint32_t chunk_start_addr = start_addr + offset;
	uint32_t chunk_size = size;

	chunk_size -= chunk_start_addr % size;

	if (num_bytes - offset + chunk_start_addr % size < size) {
		chunk_size -= size - (start_addr + num_bytes) % size;
	}
	return chunk_size;
}

static void SpiProgramPage(uint32_t address, const uint8_t *data, uint32_t num_bytes)
{
	uint32_t offset = 0;

	while (offset < num_bytes) {
		uint32_t chunk_size = MIN(SPI_TX_FIFO_DEPTH - 5, num_bytes - offset);

		SpiSingleProgram(address + offset, data + offset, chunk_size);
		offset += chunk_size;
	}
}

static void SpiProgramSector(uint32_t address, const uint8_t *data, uint32_t num_bytes)
{
	uint32_t offset = 0;

	while (offset < num_bytes) {
		uint32_t chunk_size = NextChunkSize(address, offset, num_bytes, SPI_PAGE_SIZE);

		SpiProgramPage(address + offset, data + offset, chunk_size);
		offset += chunk_size;
	}
}

static bool NeedErase(const uint8_t *new_data, const uint8_t *old_data, uint32_t size)
{
	/* Program can only clear bits, so if new_data has a bit set that is not in old_data, we
	 * must erase
	 */
	for (uint32_t i = 0; i < size; i++) {
		if (new_data[i] & ~old_data[i]) {
			return true;
		}
	}

	return false;
}

static void SpiSmartWriteSector(uint32_t address, const uint8_t *data, uint32_t num_bytes)
{
	uint32_t sector_address = address - address % SECTOR_SIZE;
	static uint8_t sector_buf[SECTOR_SIZE];

	SpiBlockRead(sector_address, SECTOR_SIZE, sector_buf);

	uint32_t offset_in_sector = address - sector_address;
	uint8_t *data_in_buf = sector_buf + offset_in_sector;

	if (memcmp(data, data_in_buf, num_bytes) == 0) {
		return;
	}

	if (NeedErase(data, data_in_buf, num_bytes)) {
		SpiEraseSector(sector_address);

		/* Program the sector from sector_buf rather than from data */
		memcpy(data_in_buf, data, num_bytes);

		address = sector_address;
		data = sector_buf;
		num_bytes = SECTOR_SIZE;
	}

	SpiProgramSector(address, data, num_bytes);
}

/* automatically erases sectors and merges incoming data with exsiting data as needed */
void SpiSmartWrite(uint32_t address, const uint8_t *data, uint32_t num_bytes)
{
	EepromClkSetup();

	uint32_t offset = 0;

	while (offset < num_bytes) {
		uint32_t chunk_size = NextChunkSize(address, offset, num_bytes, SECTOR_SIZE);

		SpiSmartWriteSector(address + offset, data + offset, chunk_size);
		offset += chunk_size;
	}
}

/* If we are using the spi buffer memory type, */
/* then make sure the passed in address and length is actually within the spi_buffer bounds. */
bool check_csm_region(uint32_t addr, uint32_t num_bytes)
{
	return addr < (uint32_t)spi_global_buffer ||
	       (addr + num_bytes) > ((uint32_t)spi_global_buffer + sizeof(spi_global_buffer));
}

static uint8_t read_eeprom_handler(uint32_t msg_code, const struct request *request,
				   struct response *response)
{
	uint8_t buffer_mem_type = BYTE_GET(request->data[0], 1);
	uint32_t spi_address = request->data[1];
	uint32_t num_bytes = request->data[2];
	uint8_t *csm_addr = (uint8_t *)request->data[3];

	if (buffer_mem_type == 0) {
		/* Make sure that we are only interacting with our csm scratch buffer */
		if (check_csm_region((uint32_t)csm_addr, num_bytes)) {
			return 2;
		}
	} else {
		/* If we aren't reading from the csm; exit with error */
		return 1;
	}

	SpiBlockRead(spi_address, num_bytes, csm_addr);
	return 0;
}

static uint8_t write_eeprom_handler(uint32_t msg_code, const struct request *request,
				    struct response *response)
{
	uint8_t buffer_mem_type = BYTE_GET(request->data[0], 1);
	uint32_t spi_address = request->data[1];
	uint32_t num_bytes = request->data[2];
	uint8_t *csm_addr = (uint8_t *)request->data[3];

	if (buffer_mem_type == 0) {
		/* Make sure that we are only interacting with our csm scratch buffer */
		if (check_csm_region((uint32_t)csm_addr, num_bytes)) {
			return 2;
		}
	} else {
		/* If we aren't reading from the csm; exit with error */
		return 1;
	}

	SpiSmartWrite(spi_address, csm_addr, num_bytes);
	return 0;
}

REGISTER_MESSAGE(MSG_TYPE_READ_EEPROM, read_eeprom_handler);
REGISTER_MESSAGE(MSG_TYPE_WRITE_EEPROM, write_eeprom_handler);
