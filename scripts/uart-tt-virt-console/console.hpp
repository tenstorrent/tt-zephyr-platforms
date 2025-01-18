/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tt
{

struct tenstorrent_get_device_info_in {
	uint32_t output_size_bytes;
};

struct tenstorrent_get_device_info_out {
	uint32_t output_size_bytes;
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;
	uint16_t bus_dev_fn;            // [0:2] function, [3:7] device, [8:15] bus
	uint16_t max_dma_buf_size_log2; // Since 1.0
	uint16_t pci_domain;            // Since 1.23
};

struct tenstorrent_mapping {
	uint32_t mapping_id;
	uint32_t reserved;
	uint64_t mapping_base;
	uint64_t mapping_size;
};

struct tenstorrent_query_mappings_in {
	uint32_t output_mapping_count;
	uint32_t reserved;
};

struct tenstorrent_query_mappings_out {
	struct tenstorrent_mapping mappings[0];
};

struct tenstorrent_query_mappings {
	struct tenstorrent_query_mappings_in in;
	struct tenstorrent_query_mappings_out out;
};

struct tenstorrent_get_device_info {
	struct tenstorrent_get_device_info_in in;
	struct tenstorrent_get_device_info_out out;
};

struct PciDeviceInfo {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t pci_domain;
	uint16_t pci_bus;
	uint16_t pci_device;
	uint16_t pci_function;
};

class BlackholePciDevice
{
	const int fd;
	const PciDeviceInfo info;
	const size_t bar0_size;

	uint8_t *bar0;

      public:
	/**
	 * @brief Construct a new BlackholePciDevice object.
	 *
	 * Opens the device file, reads the device info, and maps the BARs.
	 * TODO: We don't do any device enumeration yet.
	 *
	 * @param path  e.g. /dev/tenstorrent/0
	 */
	BlackholePciDevice(const std::string &path);

	/**
	 * @brief Destroy the BlackholePciDevice object.
	 *
	 * Unmaps the BARs and closes the device file.
	 */
	~BlackholePciDevice();

	/**
	 * @brief Information about the PCIe device as reported by TT-KMD.
	 *
	 * @return const PciDeviceInfo&
	 */
	const PciDeviceInfo &get_info() const
	{
		return info;
	}

	/**
	 * @brief Low-level access to the PCIe BARs.
	 *
	 * BAR0: 2 MiB TLB windows (x202) followed by registers, mixed WC/UC mapping
	 * BAR2: iATU (unused by us)
	 * BAR4: 4 GiB TLB windows (x8)
	 *
	 * @return uint8_t*
	 */
	uint8_t *get_bar0()
	{
		return bar0;
	}
};

} // namespace tt
