# SPDX-License-Identifier: Apache-2.0

zephyr_library()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_SMBUS_TARGET smbus_target.c)
# zephyr-keep-sorted-stop
