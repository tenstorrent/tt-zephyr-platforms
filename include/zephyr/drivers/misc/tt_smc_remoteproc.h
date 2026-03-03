/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_TT_SMC_REMOTEPROC_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_TT_SMC_REMOTEPROC_H_

/**
 * @file
 * @brief Tenstorrent SMC Remote Processor APIs
 */

/**
 * @brief Boot SMC remote processor
 *
 * Load an image to SMC remote processor SRAM, and boot it
 * @param dev Pointer to the tt_smc_remoteproc device
 * @param addr Address to load the image to
 * @param img_data Pointer to image data
 * @param img_size Size of image data
 */
int tt_smc_remoteproc_boot(const struct device *dev, uint64_t addr, uint8_t *img_data,
			   size_t img_size);

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_TT_SMC_REMOTEPROC_H_ */
