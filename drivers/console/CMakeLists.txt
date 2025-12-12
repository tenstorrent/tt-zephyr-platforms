# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025 Tenstorrent AI ULC

zephyr_library_amend()
zephyr_library_sources_ifdef(CONFIG_TENSTORRENT_VIRT_CONSOLE tt_virt_console.c)
