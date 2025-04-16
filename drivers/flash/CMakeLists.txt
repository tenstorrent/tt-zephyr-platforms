# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_include_directories("${ZEPHYR_BASE}/drivers/flash")

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_FLASH_MSPI_NOR flash_mspi_nor.c)
zephyr_library_sources_ifdef(CONFIG_FLASH_MSPI_NOR flash_mspi_nor_devs.c)
# zephyr-keep-sorted-stop
