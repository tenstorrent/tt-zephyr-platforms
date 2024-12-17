# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

zephyr_library_include_directories_ifdef(CONFIG_SMBUS ${ZEPHYR_BASE}/drivers/smbus)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_TT_SMBUS_DRIVER tt_smbus.c)
# zephyr-keep-sorted-stop

zephyr_library_property(ALLOW_EMPTY TRUE)
