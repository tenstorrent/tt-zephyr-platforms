# Copyright (c) 2019, PHYTEC Messtechnik GmbH
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_JTAG jtag_bitbang.c)
zephyr_library_sources_ifdef(CONFIG_JTAG_SHELL jtag_shell.c)
