/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BH_FWTABLE_H
#define BH_FWTABLE_H

#include <stdint.h>

/*
 * FIXME: these proto files as well as all of the entities within them need to be namespaced
 * with a tt_bh_fwtable_ prefix (or something unique and similar).
 */
#include "fw_table.pb.h"
#include "flash_info.pb.h"
#include "read_only.pb.h"

const FwTable *tt_bh_fwtable_get_fw_table(const struct device *dev);
const FlashInfoTable *tt_bh_fwtable_get_flash_info_table(const struct device *dev);
const ReadOnly *tt_bh_fwtable_get_read_only_table(const struct device *dev);

typedef enum {
	PcbTypeOrion = 0,
	PcbTypeP100 = 1,
	PcbTypeP150 = 2,
	PcbTypeP300 = 3,
	PcbTypeUBB = 4,
	PcbTypeUnknown = 0xFF
} PcbType;

PcbType tt_bh_fwtable_get_pcb_type(const struct device *dev);
uint32_t tt_bh_fwtable_get_asic_location(const struct device *dev);

#endif /* BH_FWTABLE_H*/
