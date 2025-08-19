# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(tt_banner.c)

if(CONFIG_TT_BOOT_BANNER_GIT_VERSION)

include(${ZEPHYR_BASE}/cmake/modules/git.cmake)
git_describe(${APPLICATION_SOURCE_DIR} TT_GIT_VERSION)
zephyr_library_compile_definitions(TT_GIT_VERSION="${TT_GIT_VERSION}")

endif()

if(CONFIG_TT_BOOT_BANNER_SDK_VERSION)
	zephyr_library_compile_definitions(ZEPHYR_SDK_VERSION="zephyr sdk ${SDK_VERSION}")
endif()

if(CONFIG_TT_BOOT_BANNER_FINGERPRINT)
   # Make a unique string for the fingerprint.
   string(RANDOM LENGTH 10 BUILD_FINGERPRINT)
   message("Build footprint is ${BUILD_FINGERPRINT}")
   file(WRITE ${CMAKE_BINARY_DIR}/fingerprint.txt "${BUILD_FINGERPRINT}")
   zephyr_library_compile_definitions(BUILD_FINGERPRINT="${BUILD_FINGERPRINT}")
endif()
