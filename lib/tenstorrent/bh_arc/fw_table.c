/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fw_table.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_TT_FWTABLE_IN_SPI
static FwTable fw_table;
#else
#if defined(BOARD_REVISION_P100)
/*
 * Manually extrapolated from
 * boards/tenstorrent/tt_blackhole/spirom_data_tables/P100/fw_table.txt
 */
static FwTable fw_table = {
	.fw_bundle_version = BUNDLE_VERSION_NUMBER,
	.has_chip_limits = true,
	.chip_limits = {
		.asic_fmax = 1350,
		.asic_fmin = 800,
		.tdp_limit = 150,
		.tdc_limit = 200,
		.thm_limit = 90,
		.tdc_fast_limit = 220,
	},
	.has_feature_enable = true,
	.feature_enable = {
		.cg_en = true,
		/* FIXME: enabling the options below seems to break things */
		.ddr_train_en = false,
		.aiclk_ppm_en = false,
	},
	.has_pci1_property_table = true,
	.pci1_property_table = {
		.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP,
		.num_serdes = 2,
	},
	.has_eth_property_table = true,
	.eth_property_table = {
		.eth_disable_mask = 0x3fff,
		.eth_disable_mask_en = true,
	},
};
#elif defined(BOARD_REVISION_P100A)
/*
 * Manually extrapolated from
 * boards/tenstorrent/tt_blackhole/spirom_data_tables/P100A/fw_table.txt
 */
static FwTable fw_table = {
	.fw_bundle_version = BUNDLE_VERSION_NUMBER,
	.has_chip_limits = true,
	.chip_limits = {
		.asic_fmax = 1350,
		.asic_fmin = 800,
		.tdp_limit = 150,
		.tdc_limit = 200,
		.thm_limit = 90,
		.tdc_fast_limit = 220,
	},
	.has_feature_enable = true,
	.feature_enable = {
		.cg_en = true,
		/* FIXME: enabling the options below seems to break things */
		.ddr_train_en = false,
		.aiclk_ppm_en = false,
	},
	.has_pci0_property_table = true,
	.pci0_property_table = {
		.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP,
		.num_serdes = 2,
	},
	.has_eth_property_table = true,
	.eth_property_table = {
		.eth_disable_mask = 0x3fff,
		.eth_disable_mask_en = true,
	},
};
#elif defined(BOARD_REVISION_P150A)
/*
 * Manually extrapolated from
 * boards/tenstorrent/tt_blackhole/spirom_data_tables/P150A/fw_table.txt
 */
static FwTable fw_table = {
	.fw_bundle_version = BUNDLE_VERSION_NUMBER,
	.has_chip_limits = true,
	.chip_limits = {
		.asic_fmax = 1350,
		.asic_fmin = 800,
		.tdp_limit = 150,
		.tdc_limit = 200,
		.thm_limit = 90,
		.tdc_fast_limit = 220,
	},
	.has_feature_enable = true,
	.feature_enable = {
		.cg_en = true,
		/* FIXME: enabling the options below seems to break things */
		.ddr_train_en = false,
		.aiclk_ppm_en = false,
	},
	.has_pci0_property_table = true,
	.pci0_property_table = {
		.pcie_mode = FwTable_PciPropertyTable_PcieMode_EP,
		.num_serdes = 2,
	},
};
#else
static FwTable fw_table = {
	.fw_bundle_version = BUNDLE_VERSION_NUMBER,
};
#endif
#endif

/* Loader function that deserializes the fw table bin from the SPI filesystem */
int load_fw_table(uint8_t *buffer_space, uint32_t buffer_size)
{
	if (IS_ENABLED(CONFIG_TT_FWTABLE_IN_SPI)) {
		static const char fwTableTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "cmfwcfg";
		size_t bin_size = 0;

		if (tt_boot_fs_get_file(&boot_fs_data, fwTableTag, buffer_space, buffer_size,
					&bin_size) != TT_BOOT_FS_OK) {
			return -EIO;
		}
		/* Convert the binary data to a pb_istream_t that is expected by decode */
		pb_istream_t stream = pb_istream_from_buffer(buffer_space, bin_size);
		bool status =
			pb_decode_ex(&stream, &FwTable_msg, &fw_table, PB_DECODE_NULLTERMINATED);

		if (!status) {
			/* Clear the table to avoid an inconsistent state */
			memset(&fw_table, 0, sizeof(fw_table));
			return -EIO;
		}
	}

	return 0;
}

/* Getter function that returns a const pointer to the fw table */
const FwTable *get_fw_table(void)
{
	return &fw_table;
}
