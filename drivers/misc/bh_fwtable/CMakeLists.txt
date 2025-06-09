# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(bh_fwtable)

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_BH_FWTABLE   bh_fwtable.c)
# zephyr-keep-sorted-stop

if(CONFIG_BH_FWTABLE)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

set(TT_BH_PB_DIR ${ZEPHYR_TT_ZEPHYR_PLATFORMS_MODULE_DIR}/lib/tenstorrent/bh_arc/spirom_protobufs)

zephyr_nanopb_sources(bh_fwtable
    ${TT_BH_PB_DIR}/flash_info.proto
    ${TT_BH_PB_DIR}/fw_table.proto
    ${TT_BH_PB_DIR}/read_only.proto
)

endif()
