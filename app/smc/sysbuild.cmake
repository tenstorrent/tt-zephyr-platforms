# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/bootfs_util.cmake)

# ======== bundle version setup ========
set(VERSION_FILE ${APP_DIR}/../../VERSION)
set(VERSION_TYPE BUNDLE)
include(${ZEPHYR_BASE}/cmake/modules/version.cmake)
if("${BUNDLE_VERSION_EXTRA}" STREQUAL "")
  set(BUNDLE_VERSION_EXTRA_NUM "0")
else()
  string(REGEX REPLACE "[^0-9]+" "" BUNDLE_VERSION_EXTRA_NUM "${BUNDLE_VERSION_EXTRA}")
endif()
set(BUNDLE_VERSION_STRING "${BUNDLE_VERSION_MAJOR}.${BUNDLE_VERSION_MINOR}.${BUNDLE_PATCHLEVEL}.${BUNDLE_VERSION_EXTRA_NUM}")
if("${BUNDLE_VERSION_STRING}" STREQUAL "...")
  message(FATAL_ERROR "Unable to extract bundle version")
endif()

# ======== Board validation ========
# galaxy does not require a DMC image in bootfs
if("${SB_CONFIG_DMC_BOARD}" STREQUAL "" AND NOT "${BOARD_REVISION}" STREQUAL "galaxy")
	message(FATAL_ERROR
	"Target ${BOARD}${BOARD_QUALIFIERS} not supported for this sample. "
	"There is no DMC board selected in Kconfig.sysbuild")
endif()

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

# ======== Add Recovery and DMC app ========
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

# galaxy does not have dmc image in SPI
if(NOT "${BOARD_REVISION}" STREQUAL "galaxy")
  ExternalZephyrProject_Add(
    APPLICATION dmc
    SOURCE_DIR  ${APP_DIR}/../dmc
    BOARD       ${SB_CONFIG_DMC_BOARD}
    BUILD_ONLY 1
  )
endif()

# Make sure MCUBoot is build only
set_target_properties(mcuboot PROPERTIES BUILD_ONLY 1)

# ======== Defines for filesystem generation ========
set(OUTPUT_FWBUNDLE ${CMAKE_BINARY_DIR}/update.fwbundle)

set(DMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/dmc/zephyr/zephyr.bin)
set(SMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.signed.bin)
set(RECOVERY_OUTPUT_BIN ${CMAKE_BINARY_DIR}/recovery/zephyr/zephyr.bin)
set(MCUBOOT_OUTPUT_BIN ${CMAKE_BINARY_DIR}/mcuboot/zephyr/zephyr.bin)

if (PROD_NAME MATCHES "^GALAXY")
  set(BOOTFS_DEPS ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN})
else()
  set(BOOTFS_DEPS ${DMC_OUTPUT_BIN} ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN} ${MCUBOOT_OUTPUT_BIN})
endif()

# ======== Generate filesystem ========
if (PROD_NAME MATCHES "^P300")
  foreach(side left right)
    string(TOUPPER ${side} side_upper)
    set(OUTPUT_BOOTFS_${side_upper} ${CMAKE_BINARY_DIR}/tt_boot_fs-${side}.hex)
    set(OUTPUT_FWBUNDLE_${side_upper} ${CMAKE_BINARY_DIR}/update-${side}.fwbundle)

    add_bootfs_and_fwbundle(
      ${BUNDLE_VERSION_STRING}
      ${BOARD_DIRECTORIES}/bootfs/${BOARD_REVISION}-${side}-bootfs.yaml
      ${OUTPUT_BOOTFS_${side_upper}}
      ${OUTPUT_FWBUNDLE_${side_upper}}
      ${PROD_NAME}_${side}
      ${BOOTFS_DEPS}
    )
    add_custom_target(fwbundle-${side} ALL DEPENDS ${OUTPUT_FWBUNDLE_${side_upper}})
  endforeach()

  # Merge left and right fwbundles
  add_custom_command(OUTPUT ${OUTPUT_FWBUNDLE}
    COMMAND ${PYTHON_EXECUTABLE}
    ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
    -v ${BUNDLE_VERSION_STRING}
    -o ${OUTPUT_FWBUNDLE}
    -c ${OUTPUT_FWBUNDLE_LEFT}
    -c ${OUTPUT_FWBUNDLE_RIGHT}
    DEPENDS ${OUTPUT_FWBUNDLE_LEFT} ${OUTPUT_FWBUNDLE_RIGHT})
else()
  set(OUTPUT_BOOTFS ${CMAKE_BINARY_DIR}/tt_boot_fs.hex)
  add_bootfs_and_fwbundle(
    ${BUNDLE_VERSION_STRING}
    ${BOARD_DIRECTORIES}/bootfs/${BOARD_REVISION}-bootfs.yaml
    ${OUTPUT_BOOTFS}
    ${OUTPUT_FWBUNDLE}
    ${PROD_NAME}
    ${BOOTFS_DEPS}
  )
endif()

# Add custom target that should always run, so that we will generate
# firmware bundles whenever the SMC, DMC, or recovery binaries are
# updated
add_custom_target(fwbundle ALL DEPENDS ${OUTPUT_FWBUNDLE})
