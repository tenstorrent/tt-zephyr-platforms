# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(bh_arc)
zephyr_include_directories(.)
zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
zephyr_library_sources_ifndef(
  CONFIG_TT_SMC_RECOVERY
  aiclk_ppm.c
  clock_wave.c
  dvfs.c
  eth.c
  fan_ctrl.c
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

zephyr_library_sources_ifdef(CONFIG_TT_BH_ARC_SYSINIT init.c)

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
  smbus_target.c
  spi_controller.c
  spi_eeprom.c
)
zephyr_linker_sources(DATA_SECTIONS iterables.ld)
if(CONFIG_ARC)
  zephyr_library_link_libraries(${ZEPHYR_CURRENT_MODULE_DIR}/zephyr/blobs/tt_blackhole_libpciesd.a)
endif()
