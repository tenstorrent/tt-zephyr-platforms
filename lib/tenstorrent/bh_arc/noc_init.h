/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NOC_INIT_H_INCLUDED
#define NOC_INIT_H_INCLUDED

#include <stdint.h>

#define NO_BAD_GDDR UINT8_MAX

int NocInit(void);
void InitNocTranslation(unsigned int pcie_instance, uint16_t bad_tensix_cols, uint8_t bad_gddr,
			uint16_t skip_eth);
int InitNocTranslationFromHarvesting(void);
void ClearNocTranslation(void);

#endif
