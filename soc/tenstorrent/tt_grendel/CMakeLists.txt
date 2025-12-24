# SPDX-License-Identifier: Apache-2.0

add_subdirectory(${SOC_NAME})
add_subdirectory_ifdef(CONFIG_TENSTORRENT_ROCKETCORE tt_rocketcore)
