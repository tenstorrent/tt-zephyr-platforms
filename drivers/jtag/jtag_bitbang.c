/*
 * Copyright (c) 2018-2019 PHYTEC Messtechnik GmbH
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * This file is based on SW_JTAG.c from CMSIS-DAP Source (Revision:    V2.0.0)
 * https://github.com/ARM-software/CMSIS_5/tree/develop/CMSIS/DAP/Firmware
 * Copyright (c) 2013-2017, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_jtag_gpio

#ifdef CONFIG_JTAG_USE_MMAPPED_IO
#include "jtag_ll_pin.h"
#endif

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/jtag.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define JTAG_TCK_PIN    0U
#define JTAG_TDI_PIN    1U
#define JTAG_nRESET_PIN 7U

#define CLOCK_DELAY(tck_freq, port_write_cycles)                               \
  ((CPU_CLOCK / 2 / tck_freq) - port_write_cycles)

/*
 * Default SWCLK frequency in Hz.
 * sw_clock can be used to overwrite this default value.
 */
#define JTAG_DEFAULT_TCK_FREQUENCY 1000000U
#define DELAY_SLOW_CYCLES          3U

#define CLK_SPEED   1
#define CLK_SPEEDNS 100

#define SET_TCK(config) (set_pin(config->tck_reg, config->tck.pin))
#define CLR_TCK(config) (clear_pin(config->tck_reg, config->tck.pin))

#define SET_TDI(config)      (set_pin(config->tdi_reg, config->tdi.pin))
#define CLR_TDI(config)      (clear_pin(config->tdi_reg, config->tdi.pin))
#define IF_TDI(config, stmt) (if_pin(config->tdi_reg, config->tdi.pin, stmt))

#define GET_TDO(config) (get_pin(config->tdo_reg, config->tdo.pin))

#define SET_TMS(config) (set_pin(config->tms_reg, config->tms.pin))
#define CLR_TMS(config) (clear_pin(config->tms_reg, config->tms.pin))

struct jtag_config {
	struct gpio_dt_spec tck;
	struct gpio_dt_spec tdo;
	struct gpio_dt_spec tdi;
	struct gpio_dt_spec tms;
	struct gpio_dt_spec trst;

	void *tck_reg;
	void *tdo_reg;
	void *tdi_reg;
	void *tms_reg;

	uint32_t port_write_cycles;
};

struct jtag_data {
	uint32_t clock_delay;
};

LOG_MODULE_REGISTER(jtag, CONFIG_JTAG_DRIVER_LOG_LEVEL);

static ALWAYS_INLINE int jtag_bitbang_update_ir(const struct device *dev,
                                                uint32_t count,
                                                const uint8_t *data);
static ALWAYS_INLINE int jtag_bitbang_update_dr(const struct device *dev,
                                                bool idle, uint32_t count,
                                                const uint8_t *data_in,
                                                uint8_t *data_out);

ALWAYS_INLINE void set_pin(uint32_t *reg, uint32_t pin) {
  uint32_t *output = reg + 6;

  *output = (1 << pin);
}

ALWAYS_INLINE void clear_pin(uint32_t *reg, uint32_t pin) {
  uint32_t *output = reg + 6;

  *output = (1 << (pin + 16));
}

ALWAYS_INLINE void if_pin(uint32_t *reg, uint32_t pin, bool stmt) {
  uint32_t *output = reg + 6;

  uint32_t value = stmt ? 1 : 0;
  uint32_t inv_value = stmt ? 0 : 1;

  *output = (inv_value << (pin + 16)) | (value << pin);
}

ALWAYS_INLINE int get_pin(uint32_t *reg, uint32_t pin) {
  uint32_t *input = reg + 4;
  return ((*input) >> pin) & 0x1;
}

static ALWAYS_INLINE int jtag_bitbang_tick(const struct device *dev,
                                           uint32_t count) {
  const struct jtag_config *cfg = dev->config;

  // uint32_t cycles_per_nsec = sys_clock_hw_cycles_per_sec() / (1 * 9);

  for (int i = 0; i < count; ++i) {
    // gpio_pin_set_dt(&cfg->tck, 0);
    CLR_TCK(cfg);

    // k_busy_wait(1);

    // gpio_pin_set_dt(&cfg->tck, 1);
    SET_TCK(cfg);

    // k_busy_wait(1);
  }

  // gpio_pin_set_dt(&cfg->tck, 0);
  CLR_TCK(cfg);

  return 0;
}

static int jtag_bitbang_read_id(const struct device *dev, uint32_t *id) {
  uint32_t tap_addr = 6;
  jtag_bitbang_update_ir(dev, 24, (uint8_t *)&tap_addr);
  uint32_t data_in = 0;
  jtag_bitbang_update_dr(dev, true, 32, (uint8_t *)&data_in, (uint8_t *)id);

  return 0;
}

static int jtag_bitbang_reset(const struct device *dev) {
  const struct jtag_config *cfg = dev->config;

  if (cfg->trst.port != NULL) {
    gpio_pin_set_dt(&cfg->trst, 1);
  }
  jtag_bitbang_tick(dev, 16);

  if (cfg->trst.port != NULL) {
    gpio_pin_set_dt(&cfg->trst, 0);
  }
  gpio_pin_set_dt(&cfg->tdi, 1);
  gpio_pin_set_dt(&cfg->tms, 0);

  jtag_bitbang_tick(dev, 32);

  return 0;
}

static ALWAYS_INLINE int jtag_bitbang_update_ir(const struct device *dev,
                                                uint32_t count,
                                                const uint8_t *data) {
  const struct jtag_config *cfg = dev->config;

  // Select IR scan
  // gpio_pin_set_dt(&cfg->tms, 1);
  SET_TMS(cfg);
  jtag_bitbang_tick(dev, 2);

  // Capture IR
  // gpio_pin_set_dt(&cfg->tms, 0);
  CLR_TMS(cfg);
  jtag_bitbang_tick(dev, 1);

  int i = 0;

  // Shift IR
  for (i = 0; i < count - 1; ++i) {
    // gpio_pin_set_dt(&cfg->tdi, (data[i / 8] >> (i % 8)) & 0x1);
    IF_TDI(cfg, (data[i / 8] >> (i % 8)) & 0x1);
    jtag_bitbang_tick(dev, 1);
  }

  // Exit IR
  // gpio_pin_set_dt(&cfg->tms, 0x1);
  SET_TMS(cfg);
  // gpio_pin_set_dt(&cfg->tdi, (data[i / 8] >> (i % 8)) & 0x1);
  IF_TDI(cfg, (data[i / 8] >> (i % 8)) & 0x1);
  jtag_bitbang_tick(dev, 1);

  // Select DR scan
  // gpio_pin_set_dt(&cfg->tms, 0x1);
  SET_TMS(cfg);
  jtag_bitbang_tick(dev, 2);

  return 0;
}

static ALWAYS_INLINE int
jtag_bitbang_update_dr(const struct device *dev, bool idle, uint32_t count,
                       const uint8_t *data_in, uint8_t *data_out)

{
  const struct jtag_config *cfg = dev->config;

  // Go from DR scan to shift DR state
  // gpio_pin_set_dt(&cfg->tms, 0);
  CLR_TMS(cfg);
  jtag_bitbang_tick(dev, 2);

  int i = 0;

  // Shift DR
  for (i = 0; i < count - 1; i++) {
    // gpio_pin_set_dt(&cfg->tdi, (data_in[i / 8] >> (i % 8)) & 0x1);
    IF_TDI(cfg, (data_in[i / 8] >> (i % 8)) & 0x1);
    if (data_out != NULL) {
      // data_out[i / 8] |= gpio_pin_get_dt(&cfg->tdo) << (i % 8);
      data_out[i / 8] |= GET_TDO(cfg) << (i % 8);
    }
    jtag_bitbang_tick(dev, 1);
  }

  // Exit DR
  // gpio_pin_set_dt(&cfg->tms, 1);
  SET_TMS(cfg);
  // gpio_pin_set_dt(&cfg->tdi, (data_in[i / 8] >> (i % 8)) & 0x1);
  IF_TDI(cfg, (data_in[i / 8] >> (i % 8)) & 0x1);
  if (data_out != NULL) {
    // data_out[i / 8] |= gpio_pin_get_dt(&cfg->tdo) << (i % 8);
    data_out[i / 8] |= GET_TDO(cfg) << (i % 8);
  }
  jtag_bitbang_tick(dev, 1);

  // Update DR
  // gpio_pin_set_dt(&cfg->tms, 1);
  SET_TMS(cfg);
  jtag_bitbang_tick(dev, 1);

  // If last go to run idle elase DR scan
  // gpio_pin_set_dt(&cfg->tms, idle ? 0x0 : 0x1);
  if (idle) { CLR_TMS(cfg); } else { SET_TMS(cfg); };
  jtag_bitbang_tick(dev, 1);

  return 0;
}

static int jtag_bitbang_teardown(const struct device *dev) {
  const struct jtag_config *config = dev->config;
  int ret;

  ret = gpio_pin_configure_dt(&config->tck, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tdi, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tdo, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tms, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  if (config->trst.port != NULL) {
    ret = gpio_pin_configure_dt(&config->trst, GPIO_INPUT);
  }
  if (ret) {
    return ret;
  }

  return 0;
}

static struct jtag_api jtag_bitbang_api = {.read_id = jtag_bitbang_read_id,
                                           .reset = jtag_bitbang_reset,
                                           .update_ir = jtag_bitbang_update_ir,
                                           .update_dr = jtag_bitbang_update_dr,
                                           .teardown = jtag_bitbang_teardown};

static int jtag_bitbang_init(const struct device *dev) {
  const struct jtag_config *config = dev->config;
  int ret;

  ret = gpio_pin_configure_dt(&config->tck, GPIO_OUTPUT_ACTIVE);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tdi, GPIO_OUTPUT_ACTIVE);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tdo, GPIO_INPUT);
  if (ret) {
    return ret;
  }

  ret = gpio_pin_configure_dt(&config->tms, GPIO_OUTPUT_ACTIVE);
  if (ret) {
    return ret;
  }

  if (config->trst.port != NULL) {
    ret = gpio_pin_configure_dt(&config->trst, GPIO_OUTPUT_ACTIVE);
  }
  if (ret) {
    return ret;
  }

  return 0;
}

#define JTAG_BB_GPIOS_GET_REG(n, gpios)                                        \
  COND_CODE_1(                                                                 \
      DT_INST_NODE_HAS_PROP(n, gpios),                                         \
      (INT_TO_POINTER(DT_REG_ADDR(DT_PHANDLE(DT_DRV_INST(n), gpios)))),        \
      (NULL))

#define JTAG_BB_DEVICE_DEFINE(n)                                                                   \
  static const struct jtag_config jtag_bitbang_config_##n = {                                      \
    .tck = GPIO_DT_SPEC_INST_GET(n, tck_gpios),                                                    \
    .tdi = GPIO_DT_SPEC_INST_GET(n, tdi_gpios),                                                    \
    .tdo = GPIO_DT_SPEC_INST_GET(n, tdo_gpios),                                                    \
    .tms = GPIO_DT_SPEC_INST_GET(n, tms_gpios),                                                    \
    .trst = GPIO_DT_SPEC_INST_GET_OR(n, trst_gpios, {0}),                                          \
    COND_CODE_1(CONFIG_JTAG_USE_MMAPPED_IO,                                                        \
                (.tck_reg = JTAG_BB_GPIOS_GET_REG(n, tck_gpios),                                   \
                 .tdi_reg = JTAG_BB_GPIOS_GET_REG(n, tdi_gpios),                                   \
                 .tdo_reg = JTAG_BB_GPIOS_GET_REG(n, tdo_gpios),                                   \
                 .tms_reg = JTAG_BB_GPIOS_GET_REG(n, tms_gpios),                                   \
                 .port_write_cycles = DT_INST_PROP(n, port_write_cycles), ),                       \
                ())};                                                                              \
                                                                                                   \
  static struct jtag_data jtag_bitbang_data_##n;                                                   \
                                                                                                   \
  DEVICE_DT_INST_DEFINE(n, jtag_bitbang_init, NULL, &jtag_bitbang_data_##n,                        \
                        &jtag_bitbang_config_##n, POST_KERNEL, CONFIG_JTAG_DRIVER_INIT_PRIO,       \
                        &jtag_bitbang_api);

DT_INST_FOREACH_STATUS_OKAY(JTAG_BB_DEVICE_DEFINE)
