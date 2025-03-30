# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(bh_arc)
zephyr_include_directories(.)
zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
zephyr_library_sources_ifndef(
  CONFIG_TT_SMC_RECOVERY
  aiclk_ppm.c
  clock_wave.c
  dvfs.c
  efuse.c
  eth.c
  fan_ctrl.c
  flash_info_table.c
  functional_efuse.c
  fw_table.c
  gddr.c
  harvesting.c
  i2c_messages.c
  noc.c
  noc_init.c
  pcie_dma.c
  pcie_msi.c
  pvt.c
  regulator.c
  serdes_eth.c
  telemetry.c
  telemetry_internal.c
  tensix_cg.c
  throttler.c
  vf_curve.c
  voltage.c
)

zephyr_library_sources_ifdef(CONFIG_TT_BH_ARC_SYSINIT init.c)

zephyr_library_sources(
  asic_state.c
  arc_dma.c
  avs.c
  cat.c
  cm2bm_msg.c
  dw_apb_i2c.c
  gpio.c
  init_common.c
  msgqueue.c
  noc2axi.c
  pcie.c
  pll.c
  post_code.c
  read_only_table.c
  smbus_target.c
  spi_controller.c
  spi_eeprom.c
  timer.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)
if(CONFIG_ARC)
  zephyr_library_link_libraries(${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/tt_blackhole_libpciesd.a)
endif()

include(${ZEPHYR_CURRENT_MODULE_DIR}/cmake/bundle_version.cmake)
zephyr_library_compile_definitions(BUNDLE_VERSION_NUMBER=${BUNDLE_VERSION_NUMBER})
zephyr_library_compile_definitions(BUNDLE_VERSION_STRING="${BUNDLE_VERSION_STRING}")

include(${ZEPHYR_CURRENT_MODULE_DIR}/cmake/board_revision_ifdef.cmake)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
zephyr_nanopb_sources(bh_arc spirom_protobufs/fw_table.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/read_only.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/flash_info.proto)

set(PROTOC ${PROTOBUF_PROTOC_EXECUTABLE})
set(SPIROM_PROTOBUFS ${ZEPHYR_CURRENT_MODULE_DIR}/lib/tenstorrent/bh_arc/spirom_protobufs)

message(STATUS "Generating Python files from protobufs")
set(OUTPUT_DIR ${ZEPHYR_BINARY_DIR}/python_proto_files)
file(MAKE_DIRECTORY ${OUTPUT_DIR})
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/fw_table.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/read_only.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/flash_info.proto -I ${SPIROM_PROTOBUFS}
)

if(NOT DEFINED CONFIG_TT_SMC_RECOVERY)
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

	include(${ZEPHYR_CURRENT_MODULE_DIR}/cmake/bundle_version.cmake)

	# variable is needed to avoid protobuf version clashing
	if (PROD_NAME MATCHES "^P300")
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME}_L --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME}_R --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	else()
	set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
		COMMAND PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
		  ${PYTHON_EXECUTABLE} ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_spirom_bins.py
		  --board ${PROD_NAME} --build-dir ${CMAKE_BINARY_DIR} --output ${CMAKE_BINARY_DIR}/generated_board_cfg
		  --bundle-version ${BUNDLE_VERSION_STRING}
	)
	endif()
endif()

if(NOT CONFIG_TT_SMC_RECOVERY)
if(CONFIG_TT_FWTABLE_MOCK)
  set(MY_BOARD_DIR ${BOARD_DIR})
  if(NOT BOARD STREQUAL "tt_blackhole")
    set(MY_BOARD_DIR ${ZEPHYR_CURRENT_MODULE_DIR}/boards/tenstorrent/tt_blackhole)
  endif()

  # Inputs
  set(FW_TABLE_TXT ${MY_BOARD_DIR}/spirom_data_tables/${PROD_NAME}/fw_table.txt)
  set(FW_TABLE_PROTO ${SPIROM_PROTOBUFS}/fw_table.proto)

  # Paths
  set(FW_TABLE_GEN_DIR ${ZEPHYR_BINARY_DIR}/bh_arc)
  set(FW_TABLE_MOCK_PATH ${FW_TABLE_GEN_DIR}/include)
  set(FW_TABLE_PYPATH ${FW_TABLE_GEN_DIR}/fw_table)

  # Outputs
  set(FW_TABLE_MOCK_INC ${FW_TABLE_MOCK_PATH}/tenstorrent/fw_table_mock.inc)
  set(FW_TABLE_PYLIB ${FW_TABLE_PYPATH}/fw_table_pb2.py)

  # Generate fw_table_pb2.py from fw_table.proto
  # Note: this is done as part of the build process, not as "extra_post_build_commands"
  add_custom_command(
    OUTPUT ${FW_TABLE_PYLIB}
    COMMAND
    ${PROTOC}
    --python_out=${FW_TABLE_PYPATH}
    ${FW_TABLE_PROTO}
    -I ${SPIROM_PROTOBUFS}
    DEPENDS ${FW_TABLE_PROTO}
  )
  MESSAGE(STATUS "Generated ${FW_TABLE_PYLIB}")

  # Generate fw_table_mock.inc from fw_table.txt using fw_table_pb2.py
  add_custom_command(
    OUTPUT ${FW_TABLE_MOCK_INC}
    COMMAND
    PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python
    ${PYTHON_EXECUTABLE}
    ${ZEPHYR_CURRENT_MODULE_DIR}/scripts/encode_fwtable_mock.py
    --python-path=${FW_TABLE_PYPATH}
    --bundle-version=${BUNDLE_VERSION_NUMBER}
    --input ${FW_TABLE_TXT}
    --output ${FW_TABLE_MOCK_INC}
    DEPENDS ${FW_TABLE_PYLIB}
  )
  MESSAGE(STATUS "Generated ${FW_TABLE_MOCK_INC}")

  # Create a dynamic cmake target for the generated file (required because file is not present at cmake time)
  generate_unique_target_name_from_filename(${FW_TABLE_MOCK_INC} FW_TABLE_MOCK_INC_TGT)
  add_custom_target(${FW_TABLE_MOCK_INC_TGT} DEPENDS ${FW_TABLE_MOCK_INC})

  # Add the generated file as a dependency of the bh_arc library (and update include paths)
  add_dependencies(bh_arc ${FW_TABLE_MOCK_INC_TGT})
  zephyr_library_include_directories(bh_arc ${FW_TABLE_MOCK_PATH})
endif()
endif()
