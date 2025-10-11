# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_include_directories("${ZEPHYR_BASE}/drivers/dma")

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_DMA_ARC_HS_CLUSTER dma_arc_hs_cluster.c)
# zephyr-keep-sorted-stop
