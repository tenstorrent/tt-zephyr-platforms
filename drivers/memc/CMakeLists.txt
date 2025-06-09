# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_MEMC_TT_BH_DDR memc_tt_bh_ddr.c)
# zephyr-keep-sorted-stop

if(CONFIG_MEMC_TT_BH_DDR)
zephyr_library_include_directories(../../lib/tenstorrent/bh_arc)
endif()
