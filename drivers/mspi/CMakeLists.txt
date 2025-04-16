# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_MSPI_DW mspi_dw.c)
# zephyr-keep-sorted-stop
