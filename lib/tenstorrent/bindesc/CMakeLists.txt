# Copyright (c) 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0
zephyr_library()
zephyr_library_sources_ifdef(CONFIG_TT_BINDESC tt_bindesc.c)

if(EXISTS ${APPLICATION_SOURCE_DIR}/VERSION)
  zephyr_library_compile_definitions(HAS_APP_VERSION=1)
endif()
