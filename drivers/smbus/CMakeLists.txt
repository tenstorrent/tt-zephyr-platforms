# Copyright (c) 2019, PHYTEC Messtechnik GmbH
# SPDX-License-Identifier: Apache-2.0

zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/smbus.h)

zephyr_library()

FILE(GLOB lib_sources ./*.c)

zephyr_library_sources_ifdef(CONFIG_TT_SMBUS_DRIVER ${lib_sources})
zephyr_library_include_directories_ifdef(CONFIG_TT_SMBUS_DRIVER ${ZEPHYR_BASE}/drivers/smbus)

