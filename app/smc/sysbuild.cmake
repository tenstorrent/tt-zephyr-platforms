# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# Get the bundle version
set(VERSION_FILE ${APP_DIR}/../../VERSION)
set(VERSION_TYPE BUNDLE)
include(${ZEPHYR_BASE}/cmake/modules/version.cmake)
string(REGEX REPLACE "[^0-9]+" "" BUNDLE_VERSION_EXTRA_NUM "${BUNDLE_VERSION_EXTRA}")
set(BUNDLE_VERSION_STRING "${BUNDLE_VERSION_MAJOR}.${BUNDLE_VERSION_MINOR}.${BUNDLE_PATCHLEVEL}.${BUNDLE_VERSION_EXTRA_NUM}")
if("${BUNDLE_VERSION_STRING}" STREQUAL "...")
  message(FATAL_ERROR "Unable to extract bundle version")
endif()

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
elseif(BOARD STREQUAL "native_sim")
  # Use P100 data files to stand in
  set(PROD_NAME "P100-1")
  set(BOARD_REVISION "p100")
else()
  message(FATAL_ERROR "No support for board ${BOARD}")
endif()

set(OUTPUT_BOOTFS ${CMAKE_BINARY_DIR}/tt_boot_fs.bin)
set(OUTPUT_FWBUNDLE ${CMAKE_BINARY_DIR}/update.fwbundle)

set(BMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/bmc/zephyr/zephyr.bin)
set(SMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.bin)
set(RECOVERY_OUTPUT_BIN ${CMAKE_BINARY_DIR}/recovery/zephyr/zephyr.bin)

# Generate filesystem

if (PROD_NAME MATCHES "^P300")
set(OUTPUT_BOOTFS_LEFT ${CMAKE_BINARY_DIR}/tt_boot_fs-left.bin)
set(OUTPUT_FWBUNDLE_LEFT ${CMAKE_BINARY_DIR}/update-left.fwbundle)

add_custom_command(OUTPUT ${OUTPUT_BOOTFS_LEFT}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py mkfs
  ${BOARD_DIRECTORIES}/bootfs/${BOARD_REVISION}-left-bootfs.yaml
  ${OUTPUT_BOOTFS_LEFT}
  --build-dir ${CMAKE_BINARY_DIR}
  DEPENDS ${BMC_OUTPUT_BIN} ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN})

# Generate firmware bundle that can be used to flash this build on a board
# using tt-flash
add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE_LEFT}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
  -v "${BUNDLE_VERSION_STRING}"
  -o ${OUTPUT_FWBUNDLE_LEFT}
  ${PROD_NAME}_left
  ${OUTPUT_BOOTFS_LEFT}
  DEPENDS ${OUTPUT_BOOTFS_LEFT})

# Add custom target that should always run, so that we will generate
# firmware bundles whenever the SMC, BMC, or recovery binaries are
# updated
add_custom_target(fwbundle-left ALL DEPENDS ${OUTPUT_FWBUNDLE_LEFT})

set(OUTPUT_BOOTFS_RIGHT ${CMAKE_BINARY_DIR}/tt_boot_fs-right.bin)
set(OUTPUT_FWBUNDLE_RIGHT ${CMAKE_BINARY_DIR}/update-right.fwbundle)

add_custom_command(OUTPUT ${OUTPUT_BOOTFS_RIGHT}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py mkfs
  ${BOARD_DIRECTORIES}/bootfs/${BOARD_REVISION}-right-bootfs.yaml
  ${OUTPUT_BOOTFS_RIGHT}
  --build-dir ${CMAKE_BINARY_DIR}
  DEPENDS ${BMC_OUTPUT_BIN} ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN})

# Generate firmware bundle that can be used to flash this build on a board
# using tt-flash
add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE_RIGHT}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
  -v "${BUNDLE_VERSION_STRING}"
  -o ${OUTPUT_FWBUNDLE_RIGHT}
  ${PROD_NAME}_right
  ${OUTPUT_BOOTFS_RIGHT}
  DEPENDS ${OUTPUT_BOOTFS_RIGHT})

# Add custom target that should always run, so that we will generate
# firmware bundles whenever the SMC, BMC, or recovery binaries are
# updated
add_custom_target(fwbundle-right ALL DEPENDS ${OUTPUT_FWBUNDLE_RIGHT})

# Merge left and right fwbundles
add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
  -v "${BUNDLE_VERSION_STRING}"
  -o ${OUTPUT_FWBUNDLE}
  -c ${OUTPUT_FWBUNDLE_LEFT}
  -c ${OUTPUT_FWBUNDLE_RIGHT}
  DEPENDS ${OUTPUT_FWBUNDLE_LEFT} ${OUTPUT_FWBUNDLE_RIGHT})

add_custom_target(fwbundle ALL DEPENDS ${OUTPUT_FWBUNDLE})

else()
add_custom_command(OUTPUT ${OUTPUT_BOOTFS}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py mkfs
  ${BOARD_DIRECTORIES}/bootfs/${BOARD_REVISION}-bootfs.yaml
  ${OUTPUT_BOOTFS}
  --build-dir ${CMAKE_BINARY_DIR}
  DEPENDS ${BMC_OUTPUT_BIN} ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN})

# Generate firmware bundle that can be used to flash this build on a board
# using tt-flash
add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE}
  COMMAND ${PYTHON_EXECUTABLE}
  ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
  -v "${BUNDLE_VERSION_STRING}"
  -o ${OUTPUT_FWBUNDLE}
  ${PROD_NAME}
  ${OUTPUT_BOOTFS}
  DEPENDS ${OUTPUT_BOOTFS})

# Add custom target that should always run, so that we will generate
# firmware bundles whenever the SMC, BMC, or recovery binaries are
# updated
add_custom_target(fwbundle ALL DEPENDS ${OUTPUT_FWBUNDLE})
endif()
