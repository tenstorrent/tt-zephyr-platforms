# SPDX-License-Identifier: Apache-2.0
zephyr_include_directories(.)

# Add SOC setup
zephyr_library()
zephyr_library_sources(soc.c)
