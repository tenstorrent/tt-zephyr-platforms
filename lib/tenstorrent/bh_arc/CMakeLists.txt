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
  functional_efuse.c
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

zephyr_library_sources(
  asic_state.c
  arc_dma.c
  avs.c
  cat.c
  cm2dm_msg.c
  dw_apb_i2c.c
  init.c
  msgqueue.c
  noc2axi.c
  pcie.c
  pll.c
  post_code.c
  smbus_target.c
  spi_eeprom.c
  timer.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)
if(CONFIG_ARC)
  zephyr_library_link_libraries(${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/tt_blackhole_libpciesd.a)
endif()

zephyr_library_include_directories(${CMAKE_BINARY_DIR}/modules/tt-zephyr-platforms/drivers/misc/bh_fwtable)

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
