# SPDX-License-Identifier: Apache-2.0

# zephyr-keep-sorted-start
add_subdirectory_ifdef(CONFIG_TT_BH_ARC bh_arc)
add_subdirectory_ifdef(CONFIG_TT_BH_CHIP bh_chip)
add_subdirectory_ifdef(CONFIG_TT_BIST bist)
add_subdirectory_ifdef(CONFIG_TT_BOOT_BANNER banner)
add_subdirectory_ifdef(CONFIG_TT_BOOT_FS boot_fs)
add_subdirectory_ifdef(CONFIG_TT_FAN_CTRL fan_ctrl)
add_subdirectory_ifdef(CONFIG_TT_FWUPDATE fwupdate)
add_subdirectory_ifdef(CONFIG_TT_JTAG_BOOTROM jtag_bootrom)
# zephyr-keep-sorted-stop
