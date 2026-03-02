# SPDX-License-Identifier: Apache-2.0

# Configure pyocd runner for STM32U385
board_runner_args(pyocd "--target=stm32u385rgtxq")

# Include common pyocd board configuration
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
