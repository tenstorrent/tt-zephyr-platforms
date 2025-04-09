# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(tt_banner.c)

if(CONFIG_TT_BOOT_BANNER_GIT_VERSION)

include(${ZEPHYR_BASE}/cmake/modules/git.cmake)
git_describe(. TT_GIT_VERSION)
zephyr_library_compile_definitions(TT_GIT_VERSION="${TT_GIT_VERSION}")

endif()
