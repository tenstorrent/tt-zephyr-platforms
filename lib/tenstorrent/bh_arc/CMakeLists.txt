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


# Get the bundle version
set(VERSION_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../../../VERSION)
set(VERSION_TYPE BUNDLE)
include(${ZEPHYR_BASE}/cmake/modules/version.cmake)
string(REGEX REPLACE "[^0-9]+" "" BUNDLE_VERSION_EXTRA_NUM "${BUNDLE_VERSION_EXTRA}")
set(BUNDLE_VERSION_STRING "${BUNDLE_VERSION_MAJOR}.${BUNDLE_VERSION_MINOR}.${BUNDLE_PATCHLEVEL}.${BUNDLE_VERSION_EXTRA_NUM}")
if("${BUNDLE_VERSION_STRING}" STREQUAL "...")
  message(FATAL_ERROR "Unable to extract bundle version")
endif()
message(STATUS "Bundle Version: ${BUNDLE_VERSION_STRING} (${BUNDLE_VERSION_NUMBER})")
zephyr_library_compile_definitions(BUNDLE_VERSION_NUMBER=${BUNDLE_VERSION_NUMBER})
zephyr_library_compile_definitions(BUNDLE_VERSION_STRING="${BUNDLE_VERSION_STRING}")

if(CONFIG_BOARD_REVISION STREQUAL "p100")
  zephyr_library_compile_definitions(BOARD_REVISION_P100)
endif()
if(CONFIG_BOARD_REVISION STREQUAL "p100a")
  zephyr_library_compile_definitions(BOARD_REVISION_P100A)
endif()
if(CONFIG_BOARD_REVISION STREQUAL "p150a")
  zephyr_library_compile_definitions(BOARD_REVISION_P150A)
endif()

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
