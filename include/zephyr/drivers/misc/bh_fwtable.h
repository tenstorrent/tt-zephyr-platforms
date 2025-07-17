/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BH_FWTABLE_H
#define BH_FWTABLE_H

#include <limits.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * FIXME: these proto files as well as all of the entities within them need to be namespaced
 * with a tt_bh_fwtable_ prefix (or something unique and similar).
 */
#include "fw_table.pb.h"
#include "flash_info.pb.h"
#include "read_only.pb.h"

/** @brief PCB types for Tenstorrent Blackhole platforms */
typedef enum {
	PcbTypeOrion = 0,     /**< Orion characterization board */
	PcbTypeP100 = 1,      /**< P100 (aka "scrappy") PCIe card */
	PcbTypeP150 = 2,      /**< P150 PCIe card (inclues p150a,b,c) */
	PcbTypeP300 = 3,      /**< P300 PCIe card (inclues p300a,b,c) */
	PcbTypeUBB = 4,       /**< UBB card on Galaxy systems */
	PcbTypeUnknown = 0xFF /**< Unknown PCB type */
} PcbType;

/**
 * @brief Fetch the "cmfwcfg" table.
 *
 * @warning if @p dev is uninitialized, this function will return a pointer to an uninitialized
 * memory.

 * @param dev bh_fwtable device.
 * @return A pointer to the FwTable structure.
 */
const FwTable *tt_bh_fwtable_get_fw_table(const struct device *dev);

/**
 * @brief Fetch the "flshinfo" table.
 *
 * @warning if @p dev is uninitialized, this function will return a pointer to an uninitialized
 * memory.
 *
 * @param dev bh_fwtable device.
 * @return A pointer to the FlashInfoTable.
 */
const FlashInfoTable *tt_bh_fwtable_get_flash_info_table(const struct device *dev);

/**
 * @brief Fetch the "boardcfg" table.
 *
 * @warning if @p dev is uninitialized, this function will return a pointer to an uninitialized
 * memory.
 *
 * @param dev bh_fwtable device.
 * @return A pointer to the `ReadOnly` structure.
 */
const ReadOnly *tt_bh_fwtable_get_read_only_table(const struct device *dev);

/** @brief Return true if the device is a P300 left chip. */
bool tt_bh_fwtable_is_p300_left_chip(void);

/**
 * @brief Fetch the PCB type.
 *
 * @warning if @p dev is uninitialized, this function will return @ref PcbTypeUnknown.
 *
 * @param dev bh_fwtable device.
 * @return The PCB type of the board.
 */
PcbType tt_bh_fwtable_get_pcb_type(const struct device *dev);

/**
 * @brief Get the ASIC location.
 *
 * The ASIC location is 0 for a single chip card or the right chip on a P300 card.
 *
 * On UBB boards, the ASIC location is an unsigned integer.
 *
 * @warning if @p dev is uninitialized, this function will return `UINT32_MAX`.
 *
 * @param dev bh_fwtable device.
 * @return The ASIC location.
 */
uint32_t tt_bh_fwtable_get_asic_location(const struct device *dev);

#ifdef __cplusplus
}
#endif

#endif /* BH_FWTABLE_H*/
