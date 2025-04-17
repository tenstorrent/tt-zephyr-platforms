# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_TT_BLACKHOLE_TT_BLACKHOLE_SMC AND CONFIG_MSPI AND CONFIG_FLASH)
zephyr_library()
zephyr_library_sources(init.c)
endif()
