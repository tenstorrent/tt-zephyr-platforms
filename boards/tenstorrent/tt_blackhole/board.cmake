# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SOC_TT_BLACKHOLE_BMC)
  board_runner_args(openocd "--config=${BOARD_DIR}/support/tt_blackhole_bmc.cfg")
  board_runner_args(jlink "--device=STM32G0b1ce")
endif()

if(CONFIG_SOC_TT_BLACKHOLE_SMC)
  board_runner_args(openocd "--config=${BOARD_DIR}/support/tt_blackhole_smc.cfg" "--use-elf")
endif()

if(CONFIG_ARM)
  board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
endif()

# Include debugger templates below (order is important)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)

if(CONFIG_ARM OR CONFIG_RISCV)
  include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
endif()

if(CONFIG_ARM)
  include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
endif()

board_set_flasher_ifnset(tt_flash)
board_finalize_runner_args(tt_flash)
