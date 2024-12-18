# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_RESET_TT_BH)

zephyr_library()

zephyr_library_sources(reset_tt_bh.c)

endif()
