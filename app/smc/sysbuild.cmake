# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

if("${SB_CONFIG_BMC_BOARD}" STREQUAL "")
	message(FATAL_ERROR
	"Target ${BOARD}${BOARD_QUALIFIERS} not supported for this sample. "
	"There is no BMC board selected in Kconfig.sysbuild")
endif()

if (TARGET recovery)
  # This cmake file is being processed again, because we add the recovery app
  # which triggers recursive processing. Skip the rest of the file.
  return()
endif()

# Add recovery config file
sysbuild_cache_set(VAR recovery_EXTRA_CONF_FILE recovery.conf)

# This command will trigger recursive processing of the file. See above
# for how we skip this
ExternalZephyrProject_Add(
	APPLICATION recovery
	SOURCE_DIR  ${APP_DIR}
	BUILD_ONLY 1
)

ExternalZephyrProject_Add(
	APPLICATION bmc
	SOURCE_DIR  ${APP_DIR}/../bmc
	BOARD       ${SB_CONFIG_BMC_BOARD}
	BUILD_ONLY 1
)

if(BOARD STREQUAL "tt_blackhole")
  # Map board revision names to folder names for spirom config data
  string(TOUPPER ${BOARD_REVISION} BASE_NAME)
  set(PROD_NAME "${BASE_NAME}-1")
  set(CFG_NAME "${BOARD_REVISION}-bootfs")
elseif(BOARD STREQUAL "native_sim")
  # Use P100 data files to stand in
  set(PROD_NAME "P100-1")
  set(CFG_NAME "p100-bootfs")
else()
  message(FATAL_ERROR "No support for board ${BOARD}")
endif()

set(OUTPUT_BOOTFS ${CMAKE_BINARY_DIR}/tt_boot_fs.bin)
set(OUTPUT_FWBUNDLE ${CMAKE_BINARY_DIR}/update.fwbundle)

set(BMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/bmc/zephyr/zephyr.bin)
set(SMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.bin)
set(RECOVERY_OUTPUT_BIN ${CMAKE_BINARY_DIR}/recovery/zephyr/zephyr.bin)

# Generate filesystem
add_custom_command(OUTPUT ${OUTPUT_BOOTFS}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py mkfs
  ${BOARD_DIRECTORIES}/bootfs/${CFG_NAME}.yaml
  ${OUTPUT_BOOTFS}
  --build-dir ${CMAKE_BINARY_DIR}
  DEPENDS ${BMC_OUTPUT_BIN} ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN})

# Generate firmware bundle that can be used to flash this build on a board
# using tt-flash
add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
  ${OUTPUT_BOOTFS}
  ${PROD_NAME}
  ${OUTPUT_FWBUNDLE}
  DEPENDS ${OUTPUT_BOOTFS})

# Add custom target that should always run, so that we will generate
# firmware bundles whenever the SMC, BMC, or recovery binaries are
# updated
add_custom_target(fwbundle ALL DEPENDS ${OUTPUT_FWBUNDLE})
