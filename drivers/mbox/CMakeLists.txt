# Copyright (c) 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_MBOX_TT_GRENDEL mbox_tt_grendel.c)
# zephyr-keep-sorted-stop
