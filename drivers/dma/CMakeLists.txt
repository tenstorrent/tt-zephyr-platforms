# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_DMA_ARC_HS dma_arc_hs.c)
# zephyr-keep-sorted-stop
