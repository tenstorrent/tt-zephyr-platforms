# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

function(add_bootfs_and_fwbundle bundle_version bootfs_yaml output_bootfs output_fwbundle prod_name)
  # Remaining arguments are bootfs_deps
  set(bootfs_deps ${ARGN})

  # Create a tt_boot_fs filesystem
  add_custom_command(OUTPUT ${output_bootfs}
    COMMAND ${PYTHON_EXECUTABLE}
    ${APP_DIR}/../../scripts/tt_boot_fs.py mkfs
    ${bootfs_yaml}
    ${output_bootfs}
    --hex
    --build-dir ${CMAKE_BINARY_DIR}
    DEPENDS ${bootfs_deps})

  # Generate firmware bundle that can be used to flash this build on a board
  # using tt-flash
  add_custom_command(OUTPUT ${output_fwbundle}
    COMMAND ${PYTHON_EXECUTABLE}
    ${APP_DIR}/../../scripts/tt_boot_fs.py fwbundle
    -v "${bundle_version}"
    -o ${output_fwbundle}
    ${prod_name}
    ${output_bootfs}
    DEPENDS ${output_bootfs})
endfunction()
