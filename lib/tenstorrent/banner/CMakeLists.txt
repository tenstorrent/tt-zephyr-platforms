# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(tt_banner.c)

if(CONFIG_TT_BOOT_BANNER_SDK_VERSION)
   zephyr_library_compile_definitions(ZEPHYR_SDK_VERSION="zephyr sdk ${SDK_VERSION}")
endif()
