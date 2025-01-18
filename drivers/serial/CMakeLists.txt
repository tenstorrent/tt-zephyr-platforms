# Copyright (c) 2024, Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SERIAL)

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_UART_TT_VIRT uart_tt_virt.c)

zephyr_library_include_directories(
  ../../lib/tenstorrent/bh_arc
)

endif()
