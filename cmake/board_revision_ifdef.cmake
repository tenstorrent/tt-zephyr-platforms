# SPDX-License-Identifier: Apache-2.0

# This is how we define e.g. BOARD_REVISION_P100 so that
# "#ifdef BOARD_REVISION_P100" works in C.

if(NOT DEFINED BOARD_REVISION)
  return()
endif()

string(TOUPPER ${BOARD_REVISION} BOARD_REVISION_DEF)

string(REGEX MATCH "^[A-Z0-9_]+$" BOARD_REVISION_DEF ${BOARD_REVISION_DEF})
if(NOT BOARD_REVISION_DEF)
  message(FATAL_ERROR "BOARD_REVISION must only use letters, numbers, and underscores")
endif()

set(BOARD_REVISION_DEF "BOARD_REVISION_${BOARD_REVISION_DEF}")
message(STATUS "Added Compile Definition: -D${BOARD_REVISION_DEF}")

zephyr_compile_definitions(${BOARD_REVISION_DEF})
