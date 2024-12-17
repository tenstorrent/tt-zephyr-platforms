/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/jtag.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "jtag.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dap, CONFIG_JTAG_LOG_LEVEL);

int jtag_setup(const struct device *const dev) {
  if (!device_is_ready(dev)) {
    LOG_ERR("SWD driver not ready");
    return -ENODEV;
  }

  return 0;
}

