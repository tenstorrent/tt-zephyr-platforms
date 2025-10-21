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
  # Use P100A data files to stand in
  set(PROD_NAME "P100A-1")
  set(BOARD_REVISION "p100a")
else()
  message(FATAL_ERROR "No support for board ${BOARD}")
endif()

# ======== Add Recovery and DMC app ========
if (TARGET recovery)
  # This cmake file is being processed again, because we add the recovery app
  # which triggers recursive processing. Skip the rest of the file.
  return()
endif()

# This command will trigger recursive processing of the file. See above
# for how we skip this
ExternalZephyrProject_Add(
	APPLICATION recovery
	SOURCE_DIR  ${APP_DIR}
	BUILD_ONLY 1
)

# Pass boot signature key file to recovery if specified
if (NOT SB_CONFIG_BOOT_SIGNATURE_KEY_FILE STREQUAL "")
  set_config_bool(recovery CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE n)
  set_config_string(recovery CONFIG_MCUBOOT_SIGNATURE_KEY_FILE "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")

  set_config_bool(smc CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE n)
  set_config_string(smc CONFIG_MCUBOOT_SIGNATURE_KEY_FILE "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
endif()

# galaxy does not have dmc image in SPI
if(NOT "${BOARD_REVISION}" STREQUAL "galaxy")
  ExternalZephyrProject_Add(
    APPLICATION dmc
    SOURCE_DIR  ${APP_DIR}/../dmc
    BOARD       ${SB_CONFIG_DMC_BOARD}
    BUILD_ONLY 1
  )
  # We need to make sure the first slot of the DMC image visable to mcuboot
  # is padded with 4KB of 0x00 bytes to allow for swap using offset.
  # Generate that padding file here
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/dmc-slot-padding.bin
    COMMAND dd if=/dev/zero bs=4096 count=1 of=${CMAKE_BINARY_DIR}/dmc-slot-padding.bin
  )
endif()

# Make sure MCUBoot is build only
set_target_properties(mcuboot PROPERTIES BUILD_ONLY 1)

# This is quite a hack- in order to get the mcuboot hook file we need to override
# image verification (which allows us to skip signature checks) into the mcuboot
# build, we need to create a tiny Zephyr module in the source directory, and
# tell sysbuild to include that module with mcuboot
set(mcuboot_EXTRA_ZEPHYR_MODULES "${CMAKE_CURRENT_LIST_DIR}/mcuboot_module" CACHE INTERNAL "mcuboot_module directory")
set_config_string(mcuboot CONFIG_BOOT_SIGNATURE_KEY_FILE "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")

# ======== Defines for filesystem generation ========
set(OUTPUT_FWBUNDLE ${CMAKE_BINARY_DIR}/update.fwbundle)

set(DMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/dmc/zephyr/zephyr.bin)
set(DMC_SLOT_BIN ${CMAKE_BINARY_DIR}/dmc-slot-padding.bin)
set(SMC_OUTPUT_BIN ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.signed.bin)
set(RECOVERY_OUTPUT_BIN ${CMAKE_BINARY_DIR}/recovery/zephyr/zephyr.signed.bin)
set(MCUBOOT_OUTPUT_BIN ${CMAKE_BINARY_DIR}/mcuboot/zephyr/zephyr.bin)
set(TRAILER_OUTPUT_CONFIRMED ${CMAKE_BINARY_DIR}/mcuboot_magic_confirmed.bin)

set(TRAILER_OUTPUT ${CMAKE_BINARY_DIR}/mcuboot_magic_test.bin)
add_custom_command(
    OUTPUT ${TRAILER_OUTPUT}
    COMMAND ${PYTHON_EXECUTABLE}
      ${APP_DIR}/../../scripts/gen-mcuboot-trailer.py ${TRAILER_OUTPUT}
)
add_custom_command(
    OUTPUT ${TRAILER_OUTPUT_CONFIRMED}
    COMMAND ${PYTHON_EXECUTABLE}
      ${APP_DIR}/../../scripts/gen-mcuboot-trailer.py ${TRAILER_OUTPUT_CONFIRMED} --confirmed
)

if("${BOARD_REVISION}" STREQUAL "galaxy")
  set(BOOTFS_DEPS ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN} ${MCUBOOT_OUTPUT_BIN} ${TRAILER_OUTPUT}
    ${TRAILER_OUTPUT_CONFIRMED})
else()
  # Non-galaxy boards have a DMC

  set(ROM_UPDATE_BIN ${CMAKE_BINARY_DIR}/dmc-rom-update/zephyr/zephyr.signed.bin)
  ExternalZephyrProject_Add(
    APPLICATION dmc-rom-update
    SOURCE_DIR  ${APP_DIR}/../dmc_rom_update
    BOARD       ${SB_CONFIG_DMC_BOARD}
    BUILD_ONLY 1
  )

  set(MCUBOOT_BL2_BIN ${CMAKE_BINARY_DIR}/mcuboot-bl2/zephyr/zephyr.bin)
  ExternalZephyrProject_Add(
    APPLICATION mcuboot-bl2
    SOURCE_DIR  ${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/zephyr
    BOARD       ${SB_CONFIG_DMC_BOARD}
    BUILD_ONLY 1
  )

  # Pass boot signature key file to dmc and mcuboot-bl2 if specified
  if (NOT SB_CONFIG_BOOT_SIGNATURE_KEY_FILE STREQUAL "")
    set_config_string(mcuboot-bl2 CONFIG_BOOT_SIGNATURE_KEY_FILE
      "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
    set_config_string(dmc CONFIG_MCUBOOT_SIGNATURE_KEY_FILE
      "${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}")
  endif()

  set(BOOTFS_DEPS ${SMC_OUTPUT_BIN} ${RECOVERY_OUTPUT_BIN} ${MCUBOOT_OUTPUT_BIN}
     ${DMC_OUTPUT_BIN} ${ROM_UPDATE_BIN} ${MCUBOOT_BL2_BIN} ${TRAILER_OUTPUT}
     ${TRAILER_OUTPUT_CONFIRMED} ${DMC_SLOT_BIN})
endif()

# ======== Generate filesystem ========
set(DTS_FILE ${CMAKE_BINARY_DIR}/${DEFAULT_IMAGE}/zephyr/zephyr.dts)
set(GEN_SCRIPT ${APP_DIR}/../../scripts/tt_boot_fs.py)
set(OUTPUT_FILE ${CMAKE_BINARY_DIR}/tt_boot_fs.yaml)

# Generates boot filesystem YAML from devicetrees
add_custom_command(
    OUTPUT ${OUTPUT_FILE}
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH=${PYTHON_DEVICETREE_SRC}:$ENV{PYTHONPATH}
      python3 ${GEN_SCRIPT}
      generate_bootfs
      --board ${BOARD_REVISION}
      --dts-file ${DTS_FILE}
      --bindings-dirs ${APP_DIR}/../../../zephyr/dts/bindings/ ${APP_DIR}/../../dts/bindings/
      --output-file ${OUTPUT_FILE}
      --build-dir ${CMAKE_BINARY_DIR}
      --blobs-dir ${APP_DIR}/../../zephyr/blobs
    DEPENDS
      ${DTS_FILE}
      ${GEN_SCRIPT}
    VERBATIM
)

# Generate boot filesystem YAML on every build
add_custom_target(
    generate_boot_yaml ALL
    DEPENDS ${OUTPUT_FILE}
)

if (PROD_NAME MATCHES "^P300")
  foreach(side left right)
    string(TOUPPER ${side} side_upper)
    set(OUTPUT_BOOTFS_${side_upper} ${CMAKE_BINARY_DIR}/tt_boot_fs-${side}.hex)
    set(OUTPUT_FWBUNDLE_${side_upper} ${CMAKE_BINARY_DIR}/update-${side}.fwbundle)

    add_bootfs_and_fwbundle(
      ${BUNDLE_VERSION_STRING}
      ${CMAKE_BINARY_DIR}/tt_boot_fs_${side}.yaml
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
    ${CMAKE_BINARY_DIR}/tt_boot_fs.yaml
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
