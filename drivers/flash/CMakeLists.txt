# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_include_directories("${ZEPHYR_BASE}/drivers/flash")

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_FLASH_SPI_DW spi_dw_flash.c)
# zephyr-keep-sorted-stop

if(CONFIG_FLASH_SPI_DW)
  zephyr_library_include_directories("${ZEPHYR_BASE}/drivers/spi")
endif()
