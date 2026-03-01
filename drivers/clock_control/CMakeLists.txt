# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/clock_control)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_EMUL  clock_control_emul.c)
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_TT_GRENDEL clock_control_tt_grendel.c)
zephyr_library_sources_ifdef(CONFIG_CLOCK_CONTROL_TT_BH clock_control_tt_bh.c)
# zephyr-keep-sorted-stop
