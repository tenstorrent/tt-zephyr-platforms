/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_MEMC_MEMC_TT_BH_H_
#define ZEPHYR_DRIVERS_MEMC_MEMC_TT_BH_H_

#include "gddr_telemetry_table.h"

#include <stddef.h>

#include <zephyr/device.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get a device pointer to a Blackhole memory controller instance.
 *
 * This macro can be used to populate an array of device pointers to all active instances of the
 * Blackhole memory controller compatible (`tenstorrent,bh-memc`).
 *
 * ```cpp
 * static const struct device *memc_devices[] = {
 *   DT_INST_FOREACH_STATUS_OKAY(MEMC_TT_BH_DEVICE_GET)
 * };
 * ```
 *
 * @param _n instance number
 * @return a device pointer to the specified instance.
 */
#define MEMC_TT_BH_DEVICE_GET(_n) DEVICE_DT_GET(DT_INST(_n, tenstorrent_bh_memc)),

/** @cond INTERNAL_HIDDEN */

struct memc_tt_bh_config {
	const struct device *pll_dev;
	const struct device *flash_dev;
	const struct device *fwtable_dev;
	uint32_t clock_channel;
	uint32_t clock_div;
	uint32_t inst;
};

/** @endcond INTERNAL_HIDDEN */

struct memc_tt_bh_api {
	int (*telemetry_get)(const struct device *dev, gddr_telemetry_table_t *gddr_telemetry);
};

static inline int memc_tt_bh_inst_get(const struct device *dev)
{
	__ASSERT_NO_MSG(dev != NULL);
	return (int)((const struct memc_tt_bh_config *)dev->config)->inst;
}

/**
 * @brief Get the GDDR telemetry information.
 *
 * @param dev Pointer to the device structure.
 * @param gddr_telemetry Pointer to the GDDR telemetry table structure.
 * @return 0 on success or a negative error code on failure.
 */
__syscall int memc_tt_bh_telemetry_get(const struct device *dev,
				       gddr_telemetry_table_t *gddr_telemetry);

#ifdef CONFIG_MEMC_TT_BH
static inline int z_impl_memc_tt_bh_telemetry_get(const struct device *dev,
						  gddr_telemetry_table_t *gddr_telemetry)
{
	const struct memc_tt_bh_api *api = dev->api;

	return api->telemetry_get(dev, gddr_telemetry);
}
#else
static inline int memc_tt_bh_telemetry_get(const struct device *dev,
					   gddr_telemetry_table_t *gddr_telemetry)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(gddr_telemetry);
	return -ENOTSUP;
}
#endif

#if CONFIG_MEMC_TT_BH
#include <zephyr/syscalls/memc_tt_bh.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_MEMC_MEMC_TT_BH_H_ */
