# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(
    jtag_bootrom.c
    reset.c
)

set(gen_dir ${ZEPHYR_BINARY_DIR}/misc/generated)

generate_inc_file_for_target(
    ${ZEPHYR_CURRENT_LIBRARY}
    ${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/tt_blackhole_nano_bootcode.bin
    ${gen_dir}/bootcode.h
)

zephyr_library_include_directories(${gen_dir})
