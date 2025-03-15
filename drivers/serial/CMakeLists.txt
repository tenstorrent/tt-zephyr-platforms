# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_UART_TT_VIRT uart_tt_virt.c)
# zephyr-keep-sorted-stop

if(CONFIG_UART_TT_VIRT)
zephyr_library_include_directories(../../lib/tenstorrent/bh_arc)
endif()
