# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rpmi_common)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

target_include_directories(app PRIVATE
  ${ZEPHYR_LIBRPMI_MODULE_DIR}/zephyr/include
  ${ZEPHYR_LIBRPMI_MODULE_DIR}/include
)
