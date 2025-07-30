/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRIPTS_TOOLING_RESCAN_H_
#define SCRIPTS_TOOLING_RESCAN_H_

/**
 * Rescan the PCIe bus for Tenstorrent devices.
 *
 * @param tt_dev_name Name of the Tenstorrent device to rescan.
 * @return 0 on success, or a negative error code on failure.
 */
int rescan_pcie(const char *tt_dev_name);

#endif /* SCRIPTS_TOOLING_RESCAN_H_ */
