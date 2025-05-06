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
#include "util.h"
#include "pll.h"

#include <zephyr/drivers/mspi.h>
#include <zephyr/drivers/flash.h>

#define SPI_PAGE_SIZE   256
#define SECTOR_SIZE     4096
#define SPI_BUFFER_SIZE 4096
#define BYTE_GET(v, b)  FIELD_GET(0xFFu << ((b) * 8), (v))

#define SSI_RX_DLY_SR_DEPTH            64
#define SPI_RX_SAMPLE_DELAY_TRAIN_ADDR 0x13FFC
#define SPI_RX_SAMPLE_DELAY_TRAIN_DATA 0xA5A55A5A

/* Temporary buffer to hold SPI page */
static uint8_t spi_page_buf[SPI_BUFFER_SIZE];
/* Global buffer for SPI programming */
static uint8_t spi_global_buffer[SPI_BUFFER_SIZE];
static struct flash_pages_info page_info;

static const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));

void EepromSetup(void)
{
	/* Setup SPI buffer address */
	WriteReg(RESET_UNIT_SCRATCH_RAM_REG_ADDR(10),
		 ((uint32_t)LOG2(SPI_BUFFER_SIZE) << 24) |
			 ((uint32_t)spi_global_buffer & 0xFFFFFF));
	/* Get flash device page size */
	flash_get_page_info_by_offs(flash, 0, &page_info);
}

int SpiBlockRead(uint32_t spi_address, uint32_t num_bytes, uint8_t *dest)
{
	if (!device_is_ready(flash)) {
		/* Flash init failed */
		return -ENODEV;
	}
	return flash_read(flash, spi_address, dest, num_bytes);
}

/* automatically erases sectors and merges incoming data with existing data as needed */
int SpiSmartWrite(uint32_t address, const uint8_t *data, uint32_t num_bytes)
{
	uint32_t sector_size = page_info.size;
	uint32_t addr = ROUND_DOWN(address, sector_size);
	uint32_t chunk_size;
	int rc;

	__ASSERT(sector_size > sizeof(spi_page_buf), "Sector size is larger than temp buffer");

	/* Phase 1: Write first chunk (may be unaligned)*/
	chunk_size = MIN((ROUND_UP((address + 1), sector_size) - address), num_bytes);
	rc = flash_read(flash, addr, spi_page_buf, sector_size);
	if (rc < 0) {
		return rc;
	}
	if (memcmp(&spi_page_buf[address - addr], data, chunk_size) != 0) {
		/* Write this block */
		memcpy(&spi_page_buf[address - addr], data, chunk_size);
		rc = flash_erase(flash, addr, sector_size);
		if (rc < 0) {
			return rc;
		}
		rc = flash_write(flash, addr, spi_page_buf, sector_size);
		if (rc < 0) {
			return rc;
		}
	}
	addr += sector_size;
	data += chunk_size;
	num_bytes -= chunk_size;

	/* Phase 2: Write aligned data  */
	while (num_bytes > sector_size) {

		rc = flash_read(flash, addr, spi_page_buf, sector_size);
		if (rc < 0) {
			return rc;
		}
		if (memcmp(spi_page_buf, data, MIN(sector_size, sizeof(spi_page_buf))) != 0) {
			/* Write this block */
			rc = flash_erase(flash, addr, sector_size);
			if (rc < 0) {
				return rc;
			}
			rc = flash_write(flash, addr, data, sector_size);
			if (rc < 0) {
				return rc;
			}
		}
		addr += sector_size;
		data += sector_size;
		num_bytes -= sector_size;
	}

	if (num_bytes == 0) {
		/* No need for phase 3 */
		return 0;
	}

	/* Phase 3: Write last chunk (may be unaligned) */
	chunk_size = num_bytes;
	rc = flash_read(flash, addr, spi_page_buf, sector_size);
	if (rc < 0) {
		return rc;
	}
	if (memcmp(spi_page_buf, data, chunk_size) != 0) {
		/* Write this block */
		memcpy(spi_page_buf, data, chunk_size);
		rc = flash_erase(flash, addr, sector_size);
		if (rc < 0) {
			return rc;
		}
		rc = flash_write(flash, addr, spi_page_buf, sector_size);
		if (rc < 0) {
			return rc;
		}
	}
	return 0;
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

	if (!device_is_ready(flash)) {
		/* Flash init failed */
		return 1;
	}
	if (buffer_mem_type == 0) {
		/* Make sure that we are only interacting with our csm scratch buffer */
		if (check_csm_region((uint32_t)csm_addr, num_bytes)) {
			return 2;
		}
	} else {
		/* If we aren't reading from the csm; exit with error */
		return 1;
	}

	return SpiBlockRead(spi_address, num_bytes, csm_addr);
}


static uint8_t write_eeprom_handler(uint32_t msg_code, const struct request *request,
				    struct response *response)
{
	uint8_t buffer_mem_type = BYTE_GET(request->data[0], 1);
	uint32_t spi_address = request->data[1];
	uint32_t num_bytes = request->data[2];
	uint8_t *csm_addr = (uint8_t *)request->data[3];

	if (!device_is_ready(flash)) {
		/* Flash init failed */
		return 1;
	}
	if (buffer_mem_type == 0) {
		/* Make sure that we are only interacting with our csm scratch buffer */
		if (check_csm_region((uint32_t)csm_addr, num_bytes)) {
			return 2;
		}
	} else {
		/* If we aren't reading from the csm; exit with error */
		return 1;
	}

	return SpiSmartWrite(spi_address, csm_addr, num_bytes);
}

REGISTER_MESSAGE(MSG_TYPE_READ_EEPROM, read_eeprom_handler);
REGISTER_MESSAGE(MSG_TYPE_WRITE_EEPROM, write_eeprom_handler);
