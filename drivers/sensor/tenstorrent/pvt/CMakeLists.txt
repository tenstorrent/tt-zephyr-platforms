# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
# zephyr-keep-sorted-start
  pvt_tt_bh.c
  pvt_tt_bh_decoder.c
  pvt_tt_bh_rtio.c
# zephyr-keep-sorted-stop
)

zephyr_library_include_directories(${ZEPHYR_TT_ZEPHYR_PLATFORMS_MODULE_DIR}/lib/tenstorrent/bh_arc)
