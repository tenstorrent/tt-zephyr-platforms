# Copyright (c) 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# We need to set our version string using `git describe` plus the contents of the
# version file, since we don't tag on mainline

include_guard(GLOBAL)
find_package(Git QUIET)

if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --dirty --exclude=* --always
    WORKING_DIRECTORY                ${APP_DIR}
    OUTPUT_VARIABLE                  DESCRIPTION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
    ERROR_VARIABLE                   stderr
    RESULT_VARIABLE                  return_code
  )
  if(return_code)
    message(STATUS "git describe failed: ${stderr}")
  elseif(NOT "${stderr}" STREQUAL "")
    message(STATUS "git describe warned: ${stderr}")
  else()
    # Save output
    set(GIT_SHA "-${DESCRIPTION}")
  endif()
endif()

set_property(TARGET app_version_h PROPERTY APP_VERSION_CUSTOMIZATION "#define" "APP_GIT_VERSION"
  "v${APP_VERSION_MAJOR}.${APP_VERSION_MINOR}.${APP_VERSION_TWEAK}${GIT_SHA}")
