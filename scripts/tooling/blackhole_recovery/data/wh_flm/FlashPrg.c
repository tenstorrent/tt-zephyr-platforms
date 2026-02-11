/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>

#include "FlashOS.h"

#include "spi_hal.h"
#include "eeprom.h"

/*
 *  Initialize Flash Programming Functions
 *    Parameter:      adr:  Device Base Address
 *                    clk:  Clock Frequency (Hz)
 *                    fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t Init(uint32_t adr, uint32_t clk, uint32_t fnc)
{
	int ret;

	ret = spi_init();
	if (ret != 0) {
		return 1; /* Initialization failed */
	}

	ret = eeprom_init();
	if (ret != 0) {
		spi_deinit(); /* Cleanup SPI if EEPROM init fails */
		return 1;
	}
	return 0;
}

/*
 *  De-Initialize Flash Programming Functions
 *    Parameter:      fnc:  Function Code (1 - Erase, 2 - Program, 3 - Verify)
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t UnInit(uint32_t fnc)
{
	return eeprom_deinit() == 0 ? 0 : 1;
}

/*
 *  Blank Check Checks if Memory is Blank
 *    Parameter:      adr:  Block Start Address
 *                    sz:   Block Size (in bytes)
 *                    pat:  Block Pattern
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t BlankCheck(uint32_t adr, uint32_t sz, uint8_t pat)
{
	return 1;
}

/*
 *  Erase complete Flash Memory
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t EraseChip(void)
{
	return eeprom_erase_chip() == 0 ? 0 : 1;
}

/*
 *  Erase Sector in Flash Memory
 *    Parameter:      adr:  Sector Address
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t EraseSector(uint32_t adr)
{
	return eeprom_erase_sector(adr) == 0 ? 0 : 1;
}

/*
 *  Program Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t ProgramPage(uint32_t adr, uint32_t sz, uint32_t *buf)
{
	return eeprom_program(adr, (const uint8_t *)buf, sz) == 0 ? 0 : 1;
}

/*
 *  Verify Page in Flash Memory
 *    Parameter:      adr:  Page Start Address
 *                    sz:   Page Size
 *                    buf:  Page Data
 *    Return Value:   0 - OK,  1 - Failed
 */
uint32_t Verify(uint32_t adr, uint32_t sz, uint32_t *buf)
{
	return 1;
}

/*
 * Custom API- not part of FLM spec!
 * Read data from EEPROM
 *   Parameter:      adr:  Start Address
 *                   sz:   Size of data to read
 *                   buf:  Buffer to store read data
 *   Return Value:   0 - OK,  1 - Failed
 */
uint32_t Read(uint32_t adr, uint32_t sz, uint32_t *buf)
{
	return eeprom_read(adr, (uint8_t *)buf, sz) == 0 ? 0 : 1;
}
