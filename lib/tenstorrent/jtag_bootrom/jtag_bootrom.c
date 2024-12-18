/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "axi.h"
#include "blackhole_offsets.h"
#include "jtag_profile_functions.h"
#include "pins.h"
#include "tenstorrent/bitrev.h"

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/jtag.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <tenstorrent/jtag_bootrom.h>

#define MST_TAP_OP_ISCAN_SEL (2)
#define MST_TAP_OP_DEVID_SEL (6)
#define TENSIX_SM_RTAP       (0x19e)
#define TENSIX_SM_SIBLEN     (4)
#define TENSIX_SM_TDRLEN     (32)

#define TENSIX_SIBLEN_PLUS_1_OR_0   ((TENSIX_SM_SIBLEN > 0) ? (TENSIX_SM_SIBLEN + 1) : 0)
#define TENSIX_TDRLEN_SIBLEN_PLUS_1 (1 + TENSIX_SM_TDRLEN + TENSIX_SM_SIBLEN)
#define SIBSHIFT(x)                 ((x) >> TENSIX_SM_SIBLEN)
#define SIBSHIFTUP(x)               ((x) << TENSIX_SM_SIBLEN)

#define INSTR_REG_BISTEN_SEL_END_0   (9)
#define INSTR_REG_BISTEN_SEL_START_0 (7)
#define INSTR_REG_BISTEN_SEL_MASK_0  (7)
#define INSTR_REG_BISTEN_SEL_MASK_1  (0x3f)

typedef struct {
  uint32_t op: 3;
  uint32_t rsvd0: 4;
  uint32_t bisten_sel_0: 3;
  uint32_t rsvd1: 7;
  uint32_t bisten_sel_1: 6;
  uint32_t rsvd2: 9;
} jtag_instr_t;

typedef union {
  jtag_instr_t f;
  uint32_t val;
} jtag_instr_u;

#ifdef CONFIG_JTAG_PROFILE_FUNCTIONS
static uint32_t io_ops;
#endif

LOG_MODULE_REGISTER(jtag_bootrom, CONFIG_TT_JTAG_BOOTROM_LOG_LEVEL);

#ifdef CONFIG_JTAG_USE_MMAPPED_IO
#define JTAG_BB_GPIOS_GET_REG(n, gpios)                                                            \
  COND_CODE_1(DT_NODE_HAS_PROP(n, gpios), (INT_TO_POINTER(DT_REG_ADDR(DT_PHANDLE(n, gpios)))),     \
              (NULL))

#define JTAG_BB_DEVICE_DEFINE(n)                                                                   \
  const uint32_t TCK_REG = (uint32_t)JTAG_BB_GPIOS_GET_REG(n, tck_gpios);                          \
  const uint32_t TDI_REG = (uint32_t)JTAG_BB_GPIOS_GET_REG(n, tdi_gpios);                          \
  const uint32_t TDO_REG = (uint32_t)JTAG_BB_GPIOS_GET_REG(n, tdo_gpios);                          \
  const uint32_t TMS_REG = (uint32_t)JTAG_BB_GPIOS_GET_REG(n, tms_gpios);                          \
                                                                                                   \
  volatile uint32_t *TCK_BSSR = (volatile uint32_t *)TCK_REG + 6;                                  \
  volatile uint32_t *TDI_BSSR = (volatile uint32_t *)TDI_REG + 6;                                  \
  volatile uint32_t *TDO_BSSR = (volatile uint32_t *)TDO_REG + 6;                                  \
  volatile uint32_t *TMS_BSSR = (volatile uint32_t *)TMS_REG + 6;                                  \
                                                                                                   \
  volatile uint32_t *TCK_OUT = (volatile uint32_t *)TCK_REG + 5;                                   \
  volatile uint32_t *TDI_OUT = (volatile uint32_t *)TDI_REG + 5;                                   \
  volatile uint32_t *TDO_OUT = (volatile uint32_t *)TDO_REG + 5;                                   \
  volatile uint32_t *TMS_OUT = (volatile uint32_t *)TMS_REG + 5;                                   \
                                                                                                   \
  volatile uint32_t *TCK_IN = (volatile uint32_t *)TCK_REG + 4;                                    \
  volatile uint32_t *TDI_IN = (volatile uint32_t *)TDI_REG + 4;                                    \
  volatile uint32_t *TDO_IN = (volatile uint32_t *)TDO_REG + 4;                                    \
  volatile uint32_t *TMS_IN = (volatile uint32_t *)TMS_REG + 4;

JTAG_BB_DEVICE_DEFINE(DT_N_INST_0_zephyr_jtag_gpio);

#define TCK_HIGH  (1 << TCK.pin)
#define TCK_LOW   (1 << (TCK.pin + 16))
#define SET_TCK() (*TCK_BSSR = TCK_HIGH)
#define CLR_TCK() (*TCK_BSSR = TCK_LOW)

#define TDI_HIGH     (1 << TDI.pin)
#define TDI_LOW      (1 << (TDI.pin + 16))
#define SET_TDI()    (*TDI_BSSR = TDI_HIGH)
#define CLR_TDI()    (*TDI_BSSR = TDI_LOW)
#define IF_TDI(stmt) (*TDI_BSSR = stmt ? TDI_HIGH : TDI_LOW)

#define TDO_MSK   (1 << TDO.pin)
#define GET_TDO() ((*TDO_IN & TDO_MSK) != 0)

#define TMS_HIGH  (1 << TMS.pin)
#define TMS_LOW   (1 << (TMS.pin + 16))
#define SET_TMS() (*TMS_BSSR = TMS_HIGH)
#define CLR_TMS() (*TMS_BSSR = TMS_LOW)

#else /* CONFIG_JTAG_USE_MMAPPED_IO */

static void SET_TCK(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TCK, 1);
  }
}
static void CLR_TCK(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TCK, 0);
  }
}

static void SET_TDI(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TDI, 1);
  }
}
static void CLR_TDI(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TDI, 0);
  }
}

#define IF_TDI(stmt)                                                                               \
  do {                                                                                             \
    if (stmt) {                                                                                    \
      SET_TDI();                                                                                   \
    } else {                                                                                       \
      CLR_TDI();                                                                                   \
    }                                                                                              \
  } while (0)

static bool GET_TDO(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    return gpio_pin_get_dt(&TDO);
  }
  return true;
}

static void SET_TMS(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TMS, 1);
  }
}
static void CLR_TMS(void)
{
  IO_OPS_INC();
  if (!IS_ENABLED(CONFIG_JTAG_PROFILE_FUNCTIONS)) {
    gpio_pin_set_dt(&TMS, 0);
  }
}

#endif /* CONFIG_JTAG_USE_MMAPPED_IO */

static ALWAYS_INLINE void jtag_bitbang_tick(uint32_t count)
{
  for (; count > 0; --count) {
    CLR_TCK();
    SET_TCK();
  }
  CLR_TCK();
}

static ALWAYS_INLINE void jtag_bitbang_reset()
{
  if (TRST.port != NULL) {
    gpio_pin_set_dt(&TRST, 1);
  }
  jtag_bitbang_tick(16);

  if (TRST.port != NULL) {
    gpio_pin_set_dt(&TRST, 0);
  }
  SET_TDI();
  CLR_TMS();

  jtag_bitbang_tick(32);
}

static ALWAYS_INLINE void jtag_bitbang_update_ir(uint32_t count, uint64_t data)
{
  // Select IR scan
  SET_TMS();
  jtag_bitbang_tick(2);

  // Capture IR
  CLR_TMS();
  jtag_bitbang_tick(1);

  // Shift IR
  for (; count > 1; --count, data >>= 1) {
    IF_TDI(data & 0x1);
    jtag_bitbang_tick(1);
  }

  // Exit IR
  SET_TMS();
  IF_TDI(data & 0x1);
  jtag_bitbang_tick(1);

  // Select DR scan
  SET_TMS();
  jtag_bitbang_tick(2);
}

static ALWAYS_INLINE uint64_t jtag_bitbang_xfer_dr(uint32_t count, uint64_t data_in, bool idle,
                                                   bool capture)
{
  if (count == 0) {
    return 0;
  }

  uint64_t starting_count = count;
  uint64_t data_out = 0;

  // DR Scan
  CLR_TMS();
  jtag_bitbang_tick(1);

  // Capture DR
  jtag_bitbang_tick(1);

  // Shift DR
  for (; count > 1; --count, data_in >>= 1) {
    IF_TDI(data_in & 0x1);
    if (capture) {
      data_out |= GET_TDO();
      data_out <<= 1;
    }
    jtag_bitbang_tick(1);
  }
  SET_TMS();
  IF_TDI(data_in & 0x1);
  if (capture) {
    data_out |= GET_TDO();
  }
  jtag_bitbang_tick(1);

  // Exit DR
  jtag_bitbang_tick(1);

  if (idle) {
    // Update DR
    CLR_TMS();
    jtag_bitbang_tick(1);

    // Idle
  } else {
    // Update DR
    jtag_bitbang_tick(1);

    // DR scan
  }

  if (capture) {
    data_out <<= 64 - starting_count;
    data_out = bitrev64(data_out);
  }

  return data_out;
}

static ALWAYS_INLINE uint64_t jtag_bitbang_capture_dr_idle(uint32_t count, uint64_t data_in)
{
  return jtag_bitbang_xfer_dr(count, data_in, true, true);
}

static ALWAYS_INLINE uint64_t jtag_bitbang_capture_dr(uint32_t count, uint64_t data_in)
{
  return jtag_bitbang_xfer_dr(count, data_in, false, true);
}

static ALWAYS_INLINE void jtag_bitbang_update_dr_idle(uint32_t count, uint64_t data_in)
{
  (void)jtag_bitbang_xfer_dr(count, data_in, true, false);
}

static ALWAYS_INLINE void jtag_bitbang_update_dr(uint32_t count, uint64_t data_in)
{
  (void)jtag_bitbang_xfer_dr(count, data_in, false, false);
}

uint32_t jtag_bitbang_read_id()
{
  uint32_t tap_addr = 6;
  jtag_bitbang_update_ir(24, tap_addr);
  return jtag_bitbang_capture_dr_idle(32, 0);
}

static int jtag_bitbang_init(void)
{
  int ret = gpio_pin_configure_dt(&TCK, GPIO_OUTPUT_ACTIVE) ||
            gpio_pin_configure_dt(&TDI, GPIO_OUTPUT_ACTIVE) ||
            gpio_pin_configure_dt(&TDO, GPIO_INPUT) ||
            gpio_pin_configure_dt(&TMS, GPIO_OUTPUT_ACTIVE);

  if (!ret && TRST.port != NULL) {
    ret = gpio_pin_configure_dt(&TRST, GPIO_OUTPUT_ACTIVE);
  }
  if (ret) {
    return ret;
  }

#ifdef CONFIG_JTAG_USE_MMAPPED_IO
  volatile uint32_t *TCK_SPEED = (volatile uint32_t *)TCK_REG + 2;
  volatile uint32_t *TDI_SPEED = (volatile uint32_t *)TDI_REG + 2;
  volatile uint32_t *TDO_SPEED = (volatile uint32_t *)TDO_REG + 2;
  volatile uint32_t *TMS_SPEED = (volatile uint32_t *)TMS_REG + 2;
  // volatile uint32_t *TRST_SPEED = (volatile uint32_t *)TRST_REG + 2;

  *TCK_SPEED = *TCK_SPEED | (0b11 << (TCK.pin * 2));
  *TDI_SPEED = *TDI_SPEED | (0b11 << (TDI.pin * 2));
  *TDO_SPEED = *TDO_SPEED | (0b11 << (TDO.pin * 2));
  *TMS_SPEED = *TMS_SPEED | (0b11 << (TMS.pin * 2));
  // *TRST_SPEED = *TRST_SPEED | (0b11 << (TRST.pin * 2));
#endif /* CONFIG_JTAG_USE_MMAPPED_IO */

  return 0;
}

static int jtag_bitbang_teardown(void)
{
  int ret = gpio_pin_configure_dt(&TCK, GPIO_INPUT) || gpio_pin_configure_dt(&TDI, GPIO_INPUT) ||
            gpio_pin_configure_dt(&TDO, GPIO_INPUT) || gpio_pin_configure_dt(&TMS, GPIO_INPUT);
  if (!ret && TRST.port != NULL) {
    ret = gpio_pin_configure_dt(&TRST, GPIO_INPUT);
  }
  if (ret) {
    return ret;
  }

#ifdef CONFIG_JTAG_USE_MMAPPED_IO
  volatile uint32_t *TCK_SPEED = (volatile uint32_t *)TCK_REG + 2;
  volatile uint32_t *TDI_SPEED = (volatile uint32_t *)TDI_REG + 2;
  volatile uint32_t *TDO_SPEED = (volatile uint32_t *)TDO_REG + 2;
  volatile uint32_t *TMS_SPEED = (volatile uint32_t *)TMS_REG + 2;

  *TCK_SPEED = *TCK_SPEED | (0b00 << (TCK.pin * 2));
  *TDI_SPEED = *TDI_SPEED | (0b00 << (TDI.pin * 2));
  *TDO_SPEED = *TDO_SPEED | (0b00 << (TDO.pin * 2));
  *TMS_SPEED = *TMS_SPEED | (0b00 << (TMS.pin * 2));
#endif /* CONFIG_JTAG_USE_MMAPPED_IO */

  return 0;
}

static ALWAYS_INLINE void jtag_setup_access(uint32_t rtap_addr)
{
  jtag_instr_u instrn;
  instrn.val = 0;
  instrn.f.op = MST_TAP_OP_ISCAN_SEL;
  instrn.f.bisten_sel_0 = rtap_addr & INSTR_REG_BISTEN_SEL_MASK_0;
  instrn.f.bisten_sel_1 =
    (rtap_addr >> (INSTR_REG_BISTEN_SEL_END_0 - INSTR_REG_BISTEN_SEL_START_0 + 1)) &
    INSTR_REG_BISTEN_SEL_MASK_1;

  jtag_bitbang_update_ir(24, instrn.val);
}

static ALWAYS_INLINE uint32_t jtag_access_rtap_tdr_idle(uint32_t rtap_addr, uint32_t tdr_addr,
                                                        uint32_t wrdata)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  return SIBSHIFT(
    jtag_bitbang_capture_dr_idle(TENSIX_TDRLEN_SIBLEN_PLUS_1, SIBSHIFTUP((uint64_t)wrdata)));
}

static ALWAYS_INLINE uint32_t jtag_access_rtap_tdr(uint32_t rtap_addr, uint32_t tdr_addr,
                                                   uint32_t wrdata)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  return SIBSHIFT(
    jtag_bitbang_capture_dr(TENSIX_TDRLEN_SIBLEN_PLUS_1, SIBSHIFTUP((uint64_t)wrdata)));
}

static ALWAYS_INLINE void jtag_wr_tensix_sm_rtap_tdr_idle(uint32_t tdr_addr, uint32_t wrvalue)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  jtag_bitbang_update_dr_idle(TENSIX_TDRLEN_SIBLEN_PLUS_1, SIBSHIFTUP((uint64_t)wrvalue));
}

static ALWAYS_INLINE void jtag_wr_tensix_sm_rtap_tdr(uint32_t tdr_addr, uint32_t wrvalue)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  jtag_bitbang_update_dr(TENSIX_TDRLEN_SIBLEN_PLUS_1, SIBSHIFTUP((uint64_t)wrvalue));
}

static ALWAYS_INLINE uint32_t jtag_rd_tensix_sm_rtap_tdr_idle(uint32_t tdr_addr)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  return SIBSHIFT(jtag_bitbang_capture_dr_idle(TENSIX_TDRLEN_SIBLEN_PLUS_1, 0));
}

static ALWAYS_INLINE uint32_t jtag_rd_tensix_sm_rtap_tdr(uint32_t tdr_addr)
{
  jtag_bitbang_update_dr(TENSIX_SIBLEN_PLUS_1_OR_0, (uint64_t)tdr_addr + 1);
  return SIBSHIFT(jtag_bitbang_capture_dr(TENSIX_TDRLEN_SIBLEN_PLUS_1, 0));
}

void jtag_req_clear()
{
  jtag_setup_access(TENSIX_SM_RTAP);

  jtag_wr_tensix_sm_rtap_tdr_idle(2, AXI_CNTL_CLEAR);
}

bool jtag_axiread(uint32_t addr, uint32_t *result)
{
  jtag_setup_access(TENSIX_SM_RTAP);

  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_ADDR_TDR, addr);

  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_CONTROL_STATUS_TDR, AXI_CNTL_READ);

  uint32_t axi_status = 1;
  for (int i = 0; i < 1000; ++i) {
    axi_status = jtag_rd_tensix_sm_rtap_tdr(ARC_AXI_CONTROL_STATUS_TDR);
    // FIXME(drosen): This reports back a fail, but things seem to work
    break;
    if ((axi_status & 0xF) != 0) {
      break;
    }
  }

  // Read data
  uint32_t axi_rddata = jtag_rd_tensix_sm_rtap_tdr_idle(ARC_AXI_DATA_TDR);

  *result = axi_rddata;
  return (axi_status & 1) != 0;
}

static bool jtag_axiwrite(uint32_t addr, uint32_t value)
{
  jtag_setup_access(TENSIX_SM_RTAP);

  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_ADDR_TDR, addr);
  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_DATA_TDR, value);

  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_CONTROL_STATUS_TDR, AXI_CNTL_WRITE);

  // Upper 16 bits contain write status; if first bit 1 then we passed otherwsie
  // fail
  return ((jtag_rd_tensix_sm_rtap_tdr_idle(ARC_AXI_CONTROL_STATUS_TDR) >> 16) & 1) != 1;
}

bool jtag_axiwait(uint32_t addr)
{
  // If we are using the emulated driver then always return true
  if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)) {
    return true;
  }

  jtag_bitbang_reset();

  jtag_setup_access(TENSIX_SM_RTAP);

  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_ADDR_TDR, addr);

  uint32_t axi_cntl = 1 << 31;                                      // Read
  jtag_wr_tensix_sm_rtap_tdr(ARC_AXI_CONTROL_STATUS_TDR, axi_cntl); // Trigger read

  uint32_t axi_status;
  for (int i = 0; i < 3; ++i) {
    axi_status = jtag_rd_tensix_sm_rtap_tdr(ARC_AXI_CONTROL_STATUS_TDR);
    if ((axi_status & 0xF) == 1) {
      break;
    }
  }

  jtag_rd_tensix_sm_rtap_tdr_idle(ARC_AXI_DATA_TDR);

  return (axi_status & 0xF) == 1;
}

uint32_t jtag_bitbang_wait_for_id()
{
  volatile uint32_t reset_id;
  do {
    jtag_bitbang_reset();
    reset_id = jtag_bitbang_read_id();
  } while (reset_id != 0x138A5);

  return reset_id;
}

static const struct gpio_dt_spec reset_mcu = GPIO_DT_SPEC_GET(DT_ALIAS(reset_mcu), gpios);
static const struct gpio_dt_spec reset_spi = GPIO_DT_SPEC_GET(DT_ALIAS(reset_spi), gpios);
static const struct gpio_dt_spec reset_power = GPIO_DT_SPEC_GET(DT_ALIAS(reset_power), gpios);
static const struct gpio_dt_spec pgood = GPIO_DT_SPEC_GET(DT_ALIAS(pgood), gpios);
#if (IS_ENABLED(CONFIG_BOARD_ORION_CB) || IS_ENABLED(CONFIG_BOARD_ORION_CB_1))
static const struct gpio_dt_spec arc_rambus_jtag_mux_sel =
  GPIO_DT_SPEC_GET(DT_ALIAS(arc_rambus_jtag_mux_sel), gpios);
static const struct gpio_dt_spec arc_l2_jtag_mux_sel =
  GPIO_DT_SPEC_GET(DT_ALIAS(arc_l2_jtag_mux_sel), gpios);
#endif

#if IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)
static const struct gpio_dt_spec preset_trigger = GPIO_DT_SPEC_GET(DT_ALIAS(preset_trigger), gpios);

static bool arc_reset = false;
static bool workaround_applied = false;
static bool reset_asap = false;
static struct k_spinlock reset_lock;

bool jtag_bootrom_needs_reset(void)
{
  return reset_asap;
}

void jtag_bootrom_force_reset(void)
{
  reset_asap = true;
}

struct k_spinlock jtag_bootrom_reset_lock(void)
{
  return reset_lock;
}

static int reset_request_bus_disable;

int *jtag_bootrom_disable_bus(void)
{
  return &reset_request_bus_disable;
}

bool was_arc_reset(void)
{
  return arc_reset;
}

void handled_arc_reset(void)
{
  arc_reset = false;
}

void gpio_asic_reset_callback(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
  reset_request_bus_disable = 1;
  K_SPINLOCK(&reset_lock) {
    if (workaround_applied) {
      jtag_bootrom_setup();
      jtag_bootrom_soft_reset_arc();
      jtag_bootrom_teardown();

      reset_asap = false;
    } else {
      reset_asap = true;
    }
    reset_request_bus_disable = 0;
  }
}

static struct gpio_callback preset_cb_data;
#endif // IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)

int jtag_bootrom_setup(void)
{
  // Only check for pgood if we aren't emulating
#if !DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)
  while (!gpio_pin_get_dt(&pgood)) {
  }
#endif

  gpio_pin_set_dt(&reset_mcu, 1);
  gpio_pin_set_dt(&reset_spi, 1);

  int ret = jtag_bitbang_init();
  if (ret) {
    // LOG_ERR("Failed to initialize DAP controller, %d", ret);
    return ret;
  }

  k_busy_wait(1000);

  gpio_pin_set_dt(&reset_spi, 0);
  gpio_pin_set_dt(&reset_mcu, 0);

  k_busy_wait(2000);

  jtag_bitbang_reset();

#if !DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)
  volatile uint32_t reset_id = jtag_bitbang_wait_for_id();
  if (reset_id != 0x138A5) {
    jtag_bitbang_teardown();
    if (reset_id == 0) {
      return -1;
    } else {
      return reset_id;
    }
  }
#endif

  jtag_bitbang_reset();

  while (!jtag_axiwait(BH_RESET_BASE + 0x60)) {
  }

  jtag_bitbang_reset();

  return 0;
}

int jtag_bootrom_init(void)
{
  int ret = false;

#if (IS_ENABLED(CONFIG_BOARD_ORION_CB) || IS_ENABLED(CONFIG_BOARD_ORION_CB_1))
  ret = gpio_pin_configure_dt(&arc_rambus_jtag_mux_sel, GPIO_OUTPUT_ACTIVE) ||
        gpio_pin_configure_dt(&arc_l2_jtag_mux_sel, GPIO_OUTPUT_ACTIVE);
#endif

  ret |= gpio_pin_configure_dt(&reset_mcu, GPIO_OUTPUT_ACTIVE) ||
         gpio_pin_configure_dt(&reset_spi, GPIO_OUTPUT_ACTIVE) ||
         gpio_pin_configure_dt(&reset_power, GPIO_INPUT) ||
         gpio_pin_configure_dt(&pgood, GPIO_INPUT);
  if (ret) {
    return ret;
  }

#if IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)
  ret = gpio_pin_configure_dt(&preset_trigger, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_interrupt_configure_dt(&preset_trigger, GPIO_INT_EDGE_TO_INACTIVE);
  if (ret) {
    return ret;
  }

  gpio_init_callback(&preset_cb_data, gpio_asic_reset_callback, BIT(preset_trigger.pin));
  gpio_add_callback(preset_trigger.port, &preset_cb_data);

  // Active LOW, so will be false if high
  if (!gpio_pin_get_dt(&preset_trigger)) {
    // If the preset trigger started high, then we came out of reset with the system thinking that
    // pcie is ready to go. We need to forcibly apply the workaround to ensure this remains true.
    reset_asap = true;
  }
#endif // IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)

  return 0;
}

int jtag_bootrom_patch_offset(const uint32_t *patch, size_t patch_len, const uint32_t start_addr)
{
#if IS_ENABLED(CONFIG_JTAG_LOAD_BOOTROM)
  jtag_bitbang_reset();

  // HALT THE ARC CORE!!!!!
  uint32_t arc_misc_cntl = 0;
  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);

  arc_misc_cntl |= (0b1111 << 4);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);
  // Reset it back to zero
  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);
  arc_misc_cntl &= ~(0b1111 << 4);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);

  // Enable gpio trien
  jtag_axiwrite(BH_RESET_BASE + 0x1A0, 0xff00);

  // Write to postcode
  jtag_axiwrite(BH_RESET_BASE + 0x60, 0xF2);

  CYCLES_ENTRY();
  for (int i = 0; i < patch_len; ++i) {
    // ICCM start addr is 0
    jtag_axiwrite(start_addr + (i * 4), patch[i]);
  }
  CYCLES_EXIT();

  jtag_axiwrite(BH_RESET_BASE + 0x60, 0xF3);

#if IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET)
  workaround_applied = true;
#endif

#endif

  return 0;
}

int jtag_bootrom_verify(const uint32_t *patch, size_t patch_len)
{
  if (!IS_ENABLED(CONFIG_JTAG_VERIFY_WRITE)) {
    return 0;
  }

  /* Confirmed matching */
  for (int i = 0; i < patch_len; ++i) {
    // ICCM start addr is 0
    uint32_t readback = 0;
    if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)) {
      jtag_bootrom_emul_axiread(i * 4, &readback);
    } else {
      jtag_axiread(i * 4, &readback);
    }

    if (patch[i] != readback) {
      printk("Bootcode mismatch at %03x. expected: %08x actual: %08x ¯\\_(ツ)_/¯\n", i * 4,
             patch[i], readback);

      jtag_axiwrite(BH_RESET_BASE + 0x60, 0x6);

      return 1;
    }
  }

  printk("Bootcode write verified! \\o/\n");

  return 0;
}

void jtag_bootrom_soft_reset_arc(void)
{
#if IS_ENABLED(CONFIG_JTAG_LOAD_BOOTROM)
  uint32_t arc_misc_cntl = 0;

  jtag_bitbang_reset();

  // HALT THE ARC CORE!!!!!
  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);

  arc_misc_cntl |= (0b1111 << 4);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);
  // Reset it back to zero
  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);
  arc_misc_cntl &= ~(0b1111 << 4);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);

  // Write reset_vector (rom_memory[0])
  jtag_axiwrite(BH_ROM_BASE, 0x84);

  // Toggle soft-reset
  // ARC_MISC_CNTL.soft_reset (12th bit)
  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);

  // Set to 1
  arc_misc_cntl = arc_misc_cntl | (1 << 12);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);

  jtag_axiread(BH_RESET_BASE + 0x100, &arc_misc_cntl);
  // Set to 0
  arc_misc_cntl = arc_misc_cntl & ~(1 << 12);
  jtag_axiwrite(BH_RESET_BASE + 0x100, arc_misc_cntl);
#ifdef CONFIG_JTAG_LOAD_ON_PRESET
  arc_reset = true;
#endif
#endif
}

void jtag_bootrom_teardown(void)
{
  // Just one more for good luck
  jtag_bitbang_reset();

  jtag_bitbang_teardown();
  // should be possible to call jtag_teardown(), but the dt device does not seem to exposed
  // jtag_teardown(DEVICE_DT_GET_OR_NULL(DT_INST(0, zephyr_jtag_gpio)));
}
