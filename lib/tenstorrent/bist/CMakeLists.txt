# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SOC_TT_BLACKHOLE_BMC tt_bist_bh_bmc.c)
