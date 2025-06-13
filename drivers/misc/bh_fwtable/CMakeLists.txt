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

set(PROTOC ${PROTOBUF_PROTOC_EXECUTABLE})
set(SPIROM_PROTOBUFS ${ZEPHYR_CURRENT_MODULE_DIR}/lib/tenstorrent/bh_arc/spirom_protobufs)

message(STATUS "Generating Python files from protobufs")
set(OUTPUT_DIR ${ZEPHYR_BINARY_DIR}/python_proto_files)
file(MAKE_DIRECTORY ${OUTPUT_DIR})
set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/fw_table.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/read_only.proto -I ${SPIROM_PROTOBUFS}
	COMMAND ${PROTOC} --python_out=${OUTPUT_DIR} ${SPIROM_PROTOBUFS}/flash_info.proto -I ${SPIROM_PROTOBUFS}
)

endif()
