#!/bin/bash

# Copyright (c) 2025 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

if [ -d "build" ]; then
    echo "Existing build directory found, removing it."
    rm -r build
fi

mkdir build
cd build
cmake ..
make -j$(nproc)
