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
void NocInitSingleTile(uint8_t noc0_x, uint8_t noc0_y);
void InitNocTranslation(unsigned int pcie_instance, uint16_t bad_tensix_cols, uint8_t bad_gddr,
			uint16_t skip_eth);
int InitNocTranslationFromHarvesting(void);
void ProgramNocTranslationSingleTile(uint8_t noc0_x, uint8_t noc0_y);
void ClearNocTranslation(void);
void DisableArcNocTranslation(void);
void EnableArcNocTranslation(void);
bool IsNocTranslationEnabled(void);
void NocLogicalToPhysical(uint8_t logical_x, uint8_t logical_y,
			  uint8_t *phys_x, uint8_t *phys_y);
void SetSingleTileClockGate(uint8_t noc0_x, uint8_t noc0_y, bool gate);

/* Returns NOC 0 coordinates of an enabled, unharvested tensix core.
 * It's guaranteed to be the same core until translation is enabled, disabled or modified.
 */
void GetEnabledTensix(uint8_t *x, uint8_t *y);

#endif
