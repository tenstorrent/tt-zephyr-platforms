# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_PLL_TT_BH pll_tt_bh.c)
# zephyr-keep-sorted-stop
