# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/gpio)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_GPIO_TT_BH gpio_tt_bh.c)
zephyr_library_sources_ifdef(CONFIG_GPIO_TT_GRENDEL grendel_gpio.c)
# zephyr-keep-sorted-stop
