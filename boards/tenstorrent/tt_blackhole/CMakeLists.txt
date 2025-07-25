# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_TT_BLACKHOLE_TT_BLACKHOLE_SMC)
  zephyr_library()
  if(CONFIG_MSPI AND CONFIG_FLASH)
    zephyr_library_sources(src/flash.c)
  endif()
  zephyr_library_sources_ifdef(CONFIG_UART_TT_VIRT src/vuart.c)
endif()
