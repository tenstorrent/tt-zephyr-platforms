# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()

# zephyr-keep-sorted-start
zephyr_library_sources_ifdef(CONFIG_MEMC_TT_BH memc_tt_bh.c)
# zephyr-keep-sorted-stop

if(CONFIG_MEMC_TT_BH)
  zephyr_syscall_header(../../include/zephyr/drivers/memc/memc_tt_bh.h)
  zephyr_library_include_directories(../../lib/tenstorrent/bh_arc)
endif()
