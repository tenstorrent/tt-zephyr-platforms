# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(bh_arc)
zephyr_include_directories(.)
zephyr_library_sources(
  aiclk_ppm.c
  arc_dma.c
  avs.c
  clock_wave.c
  cm2bm_msg.c
  debug_trace.c
  dw_apb_i2c.c
  dw_apb_uart.c
  dvfs.c
  efuse.c
  eth.c
  fan_ctrl.c
  flash_info_table.c
  fw_table.c
  gddr.c
  i2c_messages.c
  msgqueue.c
  noc_init.c
  noc.c
  noc2axi.c
  pcie.c
  pll.c
  pmm.c
  post_code.c
  gpio.c
  pvt.c
  regulator.c
  smbus_target.c
  timer.c
  tt_boot_fs.c
  pcie_dma.c
  read_only_table.c
  telemetry.c
  telemetry_internal.c
  throttler.c
  pcie_msi.c
  spi_controller.c
  spi_eeprom.c
  asic_state.c
  serdes_eth.c
  vf_curve.c
  voltage.c
  tensix_cg.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
zephyr_nanopb_sources(bh_arc spirom_protobufs/fw_table.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/read_only.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/flash_info.proto)
