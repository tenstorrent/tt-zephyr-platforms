#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

# This script builds the recovery firmware for all boards,
# outputs data files to the specified output directory.

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <output_dir> <tt_zephyr_platforms_dir>"
    exit 1
fi

OUTPUT_DIR=$(realpath $1)
TT_ZEPHYR_PLATFORMS_DIR=$2

build_recovery_fw() {
    BOARD=$1

    echo "Building DMC recovery firmware for board: $BOARD"
    west build -p always -b tt_blackhole@$BOARD/tt_blackhole/dmc --sysbuild app/dmc/
    if [ ! -d $OUTPUT_DIR/$BOARD ]; then
	    mkdir $OUTPUT_DIR/$BOARD
    fi
    srec_cat build/mcuboot/zephyr/zephyr.hex -intel build/dmc/zephyr/zephyr.signed.hex -intel \
      -o $OUTPUT_DIR/$BOARD/dmc_fw.hex -intel
    echo "DMC recovery firmware built and saved to $OUTPUT_DIR/$BOARD/dmc_fw.hex"

    echo "Building SMC recovery firmware for board: $BOARD"
    west build -p always -b tt_blackhole@$BOARD/tt_blackhole/smc --sysbuild app/smc/
    ./scripts/tt_boot_fs.py mkfs --build-dir build \
      --hex boards/tenstorrent/tt_blackhole/bootfs/$BOARD-bootfs.yaml build/smc_fw.hex
    # Drop section programming the read only data (including board ID)
    objcopy --remove-section .sec18 build/smc_fw.hex $OUTPUT_DIR/$BOARD/smc_fw.hex
    # Copy only the section that contains the read only data
    objcopy --only-section .sec18 build/smc_fw.hex $OUTPUT_DIR/$BOARD/board_id.hex

    DESCRIPTION="$(git describe)"
    cat <<EOF > "$OUTPUT_DIR/$BOARD/VERSION"
$DESCRIPTION
EOF

    cat <<EOF > "$OUTPUT_DIR/$BOARD/README.md"
# $BOARD Recovery Firmware

This directory contains $BOARD specific recovery files. These files are built from
[tt-zephyr-platforms](https://github.com/tenstorrent/tt-zephyr-platforms), based on the git tag
within the VERSION file.
EOF
}

cd "$TT_ZEPHYR_PLATFORMS_DIR"

for board in p100 p100a p150a p150b p150c; do
    build_recovery_fw $board
done
