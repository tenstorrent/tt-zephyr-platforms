# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_DMA_TT_BH_NOC dma_tt_bh_noc.c)
# zephyr-keep-sorted-stop
