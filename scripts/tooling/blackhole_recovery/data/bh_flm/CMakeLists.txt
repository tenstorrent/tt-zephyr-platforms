# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

# Use the zephyr SDK for building, since we just need ARM toolchain
find_package(Zephyr-sdk)
if(NOT Zephyr-sdk_FOUND)
  error("Zephyr SDK required to build")
endif()

# Set the target compiler
set(CMAKE_C_COMPILER ${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc)
set(CMAKE_CXX_COMPILER ${ZEPHYR_SDK_INSTALL_DIR}/arm-zephyr-eabi/bin/arm-zephyr-eabi-g++)
# Add baremetal C compilation flags
string(APPEND CMAKE_C_FLAGS "-Os -Wall -ffunction-sections -fdata-sections ")
string(APPEND CMAKE_C_FLAGS "-fpic -msingle-pic-base -mpic-register=9 ")
string(APPEND CMAKE_C_FLAGS "-fno-jump-tables -ffreestanding -nostartfiles ")
string(APPEND CMAKE_C_FLAGS "-mcpu=cortex-m0 -mthumb ")
string(APPEND CMAKE_C_FLAGS "-DSTM32G0xx -DSTM32G0B1xx -DFLASH_DRV_VERS=0 -DUSE_FULL_LL_DRIVER")
string(APPEND CMAKE_EXE_LINKER_FLAGS "-T ${CMAKE_CURRENT_LIST_DIR}/FlashAlgo.ld")

project(flm LANGUAGES C)
add_executable(spi.flm eeprom.c FlashDev.c FlashPrg.c stm32_spi_hal.c)

set(WORKSPACE_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../../../..)

# Include STM32 HAL
set(STM32_MODULE_DIR ${WORKSPACE_ROOT}/modules/hal/stm32)
target_include_directories(spi.flm PRIVATE ${STM32_MODULE_DIR}/stm32cube/stm32g0xx/soc)
target_include_directories(spi.flm PRIVATE ${STM32_MODULE_DIR}/stm32cube/stm32g0xx/drivers/include)

# Include CMSIS HAL
set(CMSIS_MODULE_DIR ${WORKSPACE_ROOT}/modules/hal/cmsis_6)
target_include_directories(spi.flm PRIVATE ${CMSIS_MODULE_DIR}/CMSIS/Core/Include)
