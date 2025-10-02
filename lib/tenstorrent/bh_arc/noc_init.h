/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NOC_INIT_H_INCLUDED
#define NOC_INIT_H_INCLUDED

#include <stdint.h>

#define NO_BAD_GDDR UINT8_MAX

int32_t set_tensix_enable(bool enable);

int NocInit(void);
void InitNocTranslation(unsigned int pcie_instance, uint16_t bad_tensix_cols, uint8_t bad_gddr,
			uint16_t skip_eth);
int InitNocTranslationFromHarvesting(void);
void ClearNocTranslation(void);

/* Returns NOC 0 coordinates of an enabled, unharvested tensix core.
 * It's guaranteed to be the same core until translation is enabled, disabled or modified.
 */
void GetEnabledTensix(uint8_t *x, uint8_t *y);

#endif
