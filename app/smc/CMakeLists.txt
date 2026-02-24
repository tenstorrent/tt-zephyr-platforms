# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_include_directories(app PRIVATE ${ZEPHYR_TT_ZEPHYR_PLATFORMS_MODULE_DIR}/lib/tenstorrent/bh_arc)
target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/modules/tt-system-firmware/drivers/misc/bh_fwtable/spirom_protobufs)
add_dependencies(app nanopb_generated_headers)
target_sources(app PRIVATE src/main.c)
