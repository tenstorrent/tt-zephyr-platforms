# SPDX-License-Identifier: Apache-2.0

# zephyr-keep-sorted-start
add_subdirectory_ifdef(CONFIG_FLASH flash)
add_subdirectory_ifdef(CONFIG_I2C i2c)
add_subdirectory_ifdef(CONFIG_JTAG jtag)
add_subdirectory_ifdef(CONFIG_MSPI mspi)
add_subdirectory_ifdef(CONFIG_RESET reset)
add_subdirectory_ifdef(CONFIG_SENSOR sensor)
add_subdirectory_ifdef(CONFIG_SERIAL serial)
add_subdirectory_ifdef(CONFIG_SMBUS smbus)
add_subdirectory_ifdef(CONFIG_WATCHDOG watchdog)
# zephyr-keep-sorted-stop
