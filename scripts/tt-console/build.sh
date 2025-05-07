#
# Copyright (c) 2025 Tenstorrent AI ULC
#
# SPDX-License-Identifier: Apache-2.0
#

./autogen.sh
./configure
make -j $(nproc)
