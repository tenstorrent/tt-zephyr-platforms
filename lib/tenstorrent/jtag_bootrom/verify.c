/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "axi.h"
#include "pins.h"

#include <string.h>

#include <tenstorrent/bitrev.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_DECLARE(jtag_bootrom);

#define REG_BITS 32
typedef uint32_t jtag_reg_t;

enum jtag_shift_reg {
  BR, /* Bypass Register */
  IR, /* Instruction Register */
  DR, /* Data Register */
};

enum jtag_state {
  RESET,
  IDLE,
  SCAN_DR,
  SCAN_IR,
  CAPTURE_DR,
  CAPTURE_IR,
  SHIFT_DR,
  SHIFT_IR,
  EXIT1_DR,
  EXIT1_IR,
  PAUSE_DR,
  PAUSE_IR,
  EXIT2_DR,
  EXIT2_IR,
  UPDATE_DR,
  UPDATE_IR,
};

static const enum jtag_state next_state[2][16] = {
  /* TMS low */
  [0] =
    {
      [RESET] = IDLE,
      [IDLE] = IDLE,
      [SCAN_DR] = CAPTURE_DR,
      [SCAN_IR] = CAPTURE_IR,
      [CAPTURE_DR] = SHIFT_DR,
      [CAPTURE_IR] = SHIFT_IR,
      [SHIFT_DR] = SHIFT_DR,
      [SHIFT_IR] = SHIFT_IR,
      [EXIT1_DR] = PAUSE_DR,
      [EXIT1_IR] = PAUSE_IR,
      [PAUSE_DR] = PAUSE_DR,
      [PAUSE_IR] = PAUSE_IR,
      [EXIT2_DR] = SHIFT_DR,
      [EXIT2_IR] = SHIFT_IR,
      [UPDATE_DR] = IDLE,
      [UPDATE_IR] = IDLE,
    },
  /* TMS high */
  [1] =
    {
      [RESET] = RESET,
      [IDLE] = SCAN_DR,
      [SCAN_DR] = SCAN_IR,
      [SCAN_IR] = RESET,
      [CAPTURE_DR] = EXIT1_DR,
      [CAPTURE_IR] = EXIT1_IR,
      [SHIFT_DR] = EXIT1_DR,
      [SHIFT_IR] = EXIT1_IR,
      [EXIT1_DR] = UPDATE_DR,
      [EXIT1_IR] = UPDATE_IR,
      [PAUSE_DR] = EXIT2_DR,
      [PAUSE_IR] = EXIT2_IR,
      [EXIT2_DR] = SHIFT_DR,
      [EXIT2_IR] = SHIFT_IR,
      [UPDATE_DR] = SCAN_DR,
      [UPDATE_IR] = SCAN_DR,
    },
};

__maybe_unused
static const char *const jtag_state_to_str[] = {
  "RESET  ", "IDLE   ", "SCAN_DR", "SCAN_IR",  "CAPT_DR ", "CAPT_IR", "SHFT_DR", "SHFT_IR",
  "EXT1_DR", "EXT1_IR", "PAUS_DR", "PAUSE_IR", "EXT2_DR",  "EXT2_IR", "UPDT_DR", "UPDT_IR",
};

static const struct device *port = DEVICE_DT_GET_OR_NULL(DT_INST(0, zephyr_gpio_emul));
static jtag_reg_t shift_reg[DR + 1];
static uint8_t shift_bits[DR + 1];
static jtag_reg_t hold_reg[DR + 1];
static enum jtag_state state = IDLE;
static enum jtag_shift_reg selected_reg = BR;
static bool tck_old = 1;
static size_t tck_count;
static struct gpio_callback gpio_emul_cb;
static bool have_axi_addr_tdr;
static uint32_t axi_addr_tdr;
static bool have_axi_data_tdr;
static uint32_t axi_data_tdr;
static uint32_t *sram;
static size_t sram_len;

static void on_tck_falling(enum jtag_state state, bool tdi);

static inline bool tck(void);
static inline bool tdi(void);
static inline bool tms(void);
static inline bool trst(void);

static void gpio_emul_callback(const struct device *port, struct gpio_callback *cb,
                               gpio_port_pins_t pins)
{
  /* This function should _only_ be called when the TCK pin changes */
  __ASSERT_NO_MSG(pins & BIT(TCK.pin));

  bool _tck = tck();
  bool _tms = tms();
  bool _tdi = tdi();

  if (_tck != tck_old) {
    tck_old = _tck;

    if (!_tck) {
      on_tck_falling(state, _tdi);
      state = next_state[_tms][state];

      // LOG_DBG("%5zu\t%u\t%u\t%s\t%x\t%x\t%x\t%x", tck_count, _tms, _tdi,
      // jtag_state_to_str[state],
      //         bitrev32(shift_reg[IR]) >> (32 - shift_bits[IR] - 1), shift_reg[DR], hold_reg[IR],
      //         hold_reg[DR]);

      ++tck_count;
    }
  }
}

void jtag_bootrom_emul_setup(uint32_t *buf, size_t buf_len)
{
  sram = buf;
  sram_len = buf_len;

  gpio_init_callback(&gpio_emul_cb, gpio_emul_callback, BIT(TCK.pin));
  gpio_add_callback(port, &gpio_emul_cb);
}

int jtag_bootrom_emul_axiread(uint32_t addr, uint32_t *value)
{
  size_t idx = addr >> LOG2(sizeof(uint32_t));

  if (idx >= sram_len) {
    LOG_ERR("Invalid address %08x", addr);
    return -EINVAL;
  }

  LOG_DBG("R: addr: %03x data: %08x", addr, sram[idx]);
  *value = sram[idx];

  return 0;
}

static void on_update_reg(void)
{
  switch (selected_reg) {
  case DR: {
    shift_bits[DR] = CLAMP(shift_bits[DR], 1, REG_BITS);
    hold_reg[DR] = bitrev32(shift_reg[DR]) >> (REG_BITS - shift_bits[DR]);

    if (hold_reg[DR] - 1 == ARC_AXI_ADDR_TDR) {
      have_axi_addr_tdr = true;
    } else if (have_axi_addr_tdr) {
      have_axi_addr_tdr = false;
      axi_addr_tdr = hold_reg[DR];
    } else if (hold_reg[DR] - 1 == ARC_AXI_DATA_TDR) {
      have_axi_data_tdr = true;
    } else if (have_axi_data_tdr) {
      have_axi_data_tdr = false;
      axi_data_tdr = hold_reg[DR];

      size_t i = axi_addr_tdr >> LOG2(sizeof(uint32_t));

      if (i < sram_len) {
        sram[i] = axi_data_tdr;
        LOG_DBG("W: addr: %03x data: %08x", axi_addr_tdr, axi_data_tdr);
      }
    }
  } break;
  case IR:
    hold_reg[IR] = bitrev32(shift_reg[IR]) >> (REG_BITS - shift_bits[IR] - 1);
    break;
  default:
    break;
  }
}

// _tms is the "incoming"" _tms value w.r.t. the state diagram
// we only take action here based on the "incoming" TMS value and not the outcoing
static void on_tck_falling(enum jtag_state state, bool _tdi)
{
  switch (state) {
  case SCAN_DR:
  case SCAN_IR:
    selected_reg = (state == SCAN_DR) ? DR : IR;
    break;
  case CAPTURE_DR:
  case CAPTURE_IR:
    shift_bits[selected_reg] = 0;
    break;
  case SHIFT_DR:
  case SHIFT_IR:
    if (!tms()) {
      shift_reg[selected_reg] <<= 1;
      shift_reg[selected_reg] |= _tdi;
      ++shift_bits[selected_reg];
    }
    break;
  case UPDATE_DR:
  case UPDATE_IR:
    on_update_reg();
    break;
  default:
    break;
  }
}

static inline bool tck(void)
{
  return gpio_emul_output_get(port, TCK.pin);
}

static inline bool tdi(void)
{
  return gpio_emul_output_get(port, TDI.pin);
}

static inline bool tms(void)
{
  return gpio_emul_output_get(port, TMS.pin);
}

static inline bool trst(void)
{
  return gpio_emul_output_get(port, TRST.pin);
}
