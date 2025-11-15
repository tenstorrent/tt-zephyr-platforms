# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_DMA_ARC_HS dma_arc_hs.c)
zephyr_library_sources_ifdef(CONFIG_DMA_TT_BH_NOC dma_tt_bh_noc.c)
# zephyr-keep-sorted-stop

zephyr_library_include_directories_ifdef(CONFIG_DMA_TT_BH_NOC
  ${ZEPHYR_TT_ZEPHYR_PLATFORMS_MODULE_DIR}/lib/tenstorrent/bh_arc)
