# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_TT_AVS tt_avs.c)
# zephyr-keep-sorted-stop

if(CONFIG_TT_AVS)
zephyr_library_include_directories(../../lib/tenstorrent/bh_arc)
zephyr_library_sources(../../lib/tenstorrent/bh_arc/pll.c)
endif()
