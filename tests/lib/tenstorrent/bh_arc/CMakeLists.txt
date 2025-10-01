# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bh_arc)


if(CONFIG_DEVICE_TEST)
    FILE(GLOB app_sources src/vf_curve.c)
else()
    FILE(GLOB app_sources src/*.c)
    list(FILTER app_sources EXCLUDE REGEX "src/vf_curve.c")
endif()
target_sources(app PRIVATE ${app_sources})
target_link_libraries(app PRIVATE bh_fwtable)
target_include_directories(app PRIVATE ../../../../include)
target_include_directories(app PRIVATE ../../../../lib/tenstorrent/bh_arc)
