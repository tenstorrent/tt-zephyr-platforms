/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_ddr_memc

#include <tenstorrent/memc_tt_bh_ddr.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(memc_tt_bh_ddr, CONFIG_MEMC_LOG_LEVEL);

struct memc_tt_bh_ddr_config {
};

struct memc_tt_bh_ddr_data {
};

static int memc_tt_bh_ddr_init(const struct device *dev)
{
	return 0;
}

#define DEFINE_MEMC_TT_BH_DDR(_inst)                                                               \
	static const struct memc_tt_bh_ddr_config memc_tt_bh_ddr_config_##_inst = {};              \
	static struct memc_tt_bh_ddr_data memc_tt_bh_ddr_data_##_inst;                             \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, memc_tt_bh_ddr_init, PM_DEVICE_DT_INST_GET(_inst),            \
			      &memc_tt_bh_ddr_data_##_inst, &memc_tt_bh_ddr_config_##_inst,        \
			      PRE_KERNEL_1, CONFIG_MEMC_INIT_PRIORITY, &memc_tt_bh_ddr_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_MEMC_TT_BH_DDR)
