# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

config DMA_ARC_HS_CLUSTER
	bool "ARC DMA driver"
	default y
	depends on DT_HAS_SNPS_DESIGNWARE_DMA_ARC_ENABLED && DMA
	help
	  DMA driver for ARC MCUs.

if DMA_ARC_HS_CLUSTER

config DMA_ARC_CHANNEL_COUNT
	int "Number of DMA channels (1, 4, 8, or 16)"
	default 1
	range 1 16
	help
	  Number of DMA channels supported by the DMA server.
	  Valid values are 1, 4, 8, or 16.

config DMA_ARC_DESCRIPTOR_COUNT
	int "Number of DMA descriptors (32, 64, 128, or 256)"
	default 32
	range 32 256
	help
	  Number of DMA descriptors supported by the DMA server.
	  Valid values are 32, 64, 128, or 256.

config DMC_ARC_MAX_BURST
	int "Maximum burst size for bus transactions (4, 8, 16)"
	default 4
	range 4 16
	help
	  Maximum burst size supported by the DMA server.
	  Valid values are 4, 8, or 16.

config DMA_ARC_MAX_TRANS
	int "Maximum number of pending bus transactions (4, 8, 16, 32)"
	default 4
	range 4 32
	help
	  Maximum number of pending bus transactions supported by the DMA server.
	  Valid values are 4, 8, 16, or 32.

config DMA_ARC_BUF_SIZE
	int "DMA server data buffer size in word entries (16, 32, 64, 128)"
	default 16
	range 16 128
	help
	  DMA server data buffer size in word entries.
	  Valid values are 16, 32, 64, or 128.

config DMA_ARC_SUPPORT_COHERENCY
	bool "DMA server support coherency check for all L1 caches within the cluster by hardware"
	default n
	help
	  Enable this option to enable coherency check for all L1 caches within the cluster by hardware.


endif # DMA_ARC_HS_CLUSTER
