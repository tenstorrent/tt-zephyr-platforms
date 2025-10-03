#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

SCRIPT_DIR=$(realpath $(dirname $0))

if [ -d "$SCRIPT_DIR/build" ]; then
    echo "Existing build directory found, removing it."
    rm -r $SCRIPT_DIR/build
fi

mkdir $SCRIPT_DIR/build
cd $SCRIPT_DIR/build
cmake ..
make -j
