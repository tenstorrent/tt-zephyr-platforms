# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr COMPONENTS unittest REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bitrev)

FILE(GLOB app_sources src/*.c)
target_sources(testbinary PRIVATE ${app_sources})
target_include_directories(testbinary PRIVATE ../../../include)
