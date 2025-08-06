/* Flash OS Routines
 * Copyright (c) 2009-2015 ARM Limited
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

#include "FlashOS.h"

#define DEVICE_NAME "External SPI EEPROM"
struct FlashDevice const FlashDevice = {
	FLASH_DRV_VERS,            /* Driver Version, do not modify! */
	DEVICE_NAME,               /* Device Name (128 chars max) */
	EXTSPI,                    /* Device Type */
	0x00000000,                /* Device Start Address */
	0x4000000,                 /* Device Size */
	0x00001000,                /* Programming Page Size */
	0x00000000,                /* Reserved, must be 0 */
	0xFF,                      /* Initial Content of Erased Memory */
	300,                       /* Program Page Timeout 100 mSec */
	3000,                      /* Erase Sector Timeout 3000 mSec */
	{{0x00001000, 0x00000000}, /* Sector Size {4kB, starting at address 0} */
	 {SECTOR_END}},
};
