# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/sensor/tenstorrent)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_PVT_TT_BH pvt_tt_bh.c pvt_tt_bh_decoder.c pvt_tt_bh_rtio.c)
# zephyr-keep-sorted-stop
