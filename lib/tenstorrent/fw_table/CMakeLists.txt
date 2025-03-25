# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(fw_table)
zephyr_library_sources(fw_table.c read_only_table.c flash_info_table.c)
# Add spirom_protobufs to system include directory, this is where protoc
# will place generated files
zephyr_include_directories(${CMAKE_CURRENT_BINARY_DIR}/spirom_protobufs)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
zephyr_nanopb_sources(fw_table spirom_protobufs/fw_table.proto)
zephyr_nanopb_sources(fw_table spirom_protobufs/read_only.proto)
zephyr_nanopb_sources(fw_table spirom_protobufs/flash_info.proto)

set(PROTOC ${PROTOBUF_PROTOC_EXECUTABLE})
set(SPIROM_PROTOBUFS ${ZEPHYR_CURRENT_MODULE_DIR}/lib/tenstorrent/fw_table/spirom_protobufs)

message(STATUS "Generating Python files from protobufs")
set(OUTPUT_DIR ${ZEPHYR_BINARY_DIR}/python_proto_files)
file(MAKE_DIRECTORY ${OUTPUT_DIR})
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/fw_table.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/read_only.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/flash_info.proto -I ${SPIROM_PROTOBUFS}
)

if(BOARD STREQUAL "tt_blackhole")
  # Map board revision names to folder name
  string(TOUPPER ${BOARD_REVISION} PROD_NAME)
elseif(BOARD STREQUAL "native_sim")
  # Use P100 data files to stand in
  set(PROD_NAME "P100")
else()
  message(FATAL_ERROR "No support for board ${BOARD}")
endif()

message(STATUS "Generating rom configuration binary files for ${PROD_NAME}")

# variable is needed to avoid protobuf version clashing
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
	COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
	  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
	  --board ${PROD_NAME} --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
)
