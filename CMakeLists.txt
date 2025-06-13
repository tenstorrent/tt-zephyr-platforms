# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(include)

zephyr_include_directories( $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/modules/tt-zephyr-platforms/drivers/misc/bh_fwtable>)

add_subdirectory(drivers)
add_subdirectory(lib)
