# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_WDT_TT_BH wdt_tt_bh.c)
# zephyr-keep-sorted-stop

if(CONFIG_WDT_TT_BH)
zephyr_library_include_directories(../../lib/tenstorrent/bh_arc)
endif()
