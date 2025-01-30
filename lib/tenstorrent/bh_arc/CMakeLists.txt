# Copyright (c) 2024 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(libpciesd)
zephyr_library_sources(pcie_priv.c)

zephyr_library_named(bh_arc)
zephyr_include_directories(.)
zephyr_library_sources_ifndef(
  CONFIG_TT_SMC_RECOVERY
  aiclk_ppm.c
  clock_wave.c
  dvfs.c
  efuse.c
  eth.c
  fan_ctrl.c
  flash_info_table.c
  functional_efuse.c
  fw_table.c
  gddr.c
  harvesting.c
  i2c_messages.c
  noc.c
  noc_init.c
  pcie_dma.c
  pcie_msi.c
  pvt.c
  regulator.c
  serdes_eth.c
  telemetry.c
  telemetry_internal.c
  tensix_cg.c
  throttler.c
  vf_curve.c
  voltage.c
)

zephyr_library_sources(
  asic_state.c
  arc_dma.c
  avs.c
  cat.c
  cm2bm_msg.c
  dw_apb_i2c.c
  gpio.c
  init_common.c
  msgqueue.c
  noc2axi.c
  pcie.c
  pll.c
  post_code.c
  read_only_table.c
  smbus_target.c
  spi_controller.c
  spi_eeprom.c
  timer.c
  tt_boot_fs.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)
zephyr_library_link_libraries(libpciesd)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)
zephyr_nanopb_sources(bh_arc spirom_protobufs/fw_table.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/read_only.proto)
zephyr_nanopb_sources(bh_arc spirom_protobufs/flash_info.proto)
