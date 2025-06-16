# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_PINCTRL_TT_BH pinctrl_tt_bh.c)
# zephyr-keep-sorted-stop
