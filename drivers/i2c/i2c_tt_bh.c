/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "i2c_dw.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT tenstorrent_bh_i2c

#define DW_APB_I2C_REG_MAP_BASE_ADDR    0x80060000
#define DW_APB_I2C_IC_ENABLE_REG_OFFSET 0x0000006c

#define RESET_UNIT_I2C_CNTL_REG_ADDR 0x800300F0

#define RESET_UNIT_I2C_PAD_CNTL_DRV_SHIFT      10
#define RESET_UNIT_I2C_PAD_CNTL_RXEN_MASK      0xC0
#define RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK 0x1
#define RESET_UNIT_I2C_PAD_CTRL_TRIEN_SDA_MASK 0x2
#define RESET_UNIT_I2C_PAD_CNTL_TRIEN_MASK                                                         \
	(RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK | RESET_UNIT_I2C_PAD_CTRL_TRIEN_SDA_MASK)

BUILD_ASSERT(CONFIG_I2C_TT_BH_INIT_PRIORITY > CONFIG_I2C_INIT_PRIORITY,
	     "I2C TT BH driver must be initialized after the Designware I2C driver");

LOG_MODULE_REGISTER(i2c_tt_bh, CONFIG_I2C_LOG_LEVEL);

struct i2c_tt_bh_config {
	const struct device *dw_i2c_dev;
	const uintptr_t pad_cntl;
	const uintptr_t pad_data;
	uint8_t id;
};

static int i2c_tt_bh_recover_bus(const struct device *dev)
{
	uintptr_t ena_addr = DW_APB_I2C_REG_MAP_BASE_ADDR + DW_APB_I2C_IC_ENABLE_REG_OFFSET;
	const struct i2c_tt_bh_config *config = dev->config;
	/* FIXME: backup and restore previously configured drive strength when pinctrl is fixed */
	uint32_t drive_strength = 0x7F; /* 50% of max 0xFF */
	uint32_t i2c_cntl = (drive_strength << RESET_UNIT_I2C_PAD_CNTL_DRV_SHIFT) |
			    RESET_UNIT_I2C_PAD_CNTL_TRIEN_MASK;
	uint32_t i2c_rst_cntl = sys_read32(RESET_UNIT_I2C_CNTL_REG_ADDR);

	/* Disable I2C controller */
	sys_write32(ena_addr, 0);
	/* Release control of pads from I2C controller */
	sys_write32(RESET_UNIT_I2C_CNTL_REG_ADDR, i2c_rst_cntl & ~BIT(config->id));
	/* Init I2C pads for I2C controller */
	sys_write32(config->pad_cntl, i2c_cntl);
	/* Set both pads to output low */
	sys_write32(config->pad_data, 0);
	/*
	 * First, manually hold SCL low for 150 ms. Per the SMBUS spec,
	 * we should only need to hold the line low for 25 ms, but that does
	 * not work reliably and this does...
	 */
	i2c_cntl ^= RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK;
	sys_write32(config->pad_cntl, i2c_cntl);
	k_sleep(K_MSEC(150));
	/*
	 * Bitbang I2C reset to unstick bus. Hold SDA low, toggle SCL 32 times to create 16
	 * clock cycles. Note we toggle the TRIEN bit, as when TRIEN is
	 * set the bus will be released and external pullups will
	 * drive SCL high.
	 */
	for (int i = 0; i < 32; i++) {
		i2c_cntl ^= RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK;
		sys_write32(config->pad_cntl, i2c_cntl);
		k_sleep(K_USEC(100));
	}
	/* Add stop condition- transition SDA to high while SCL is high. */
	sys_write32(config->pad_cntl, RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK);
	k_sleep(K_USEC(100));
	sys_write32(config->pad_cntl, RESET_UNIT_I2C_PAD_CTRL_TRIEN_SCL_MASK |
					      RESET_UNIT_I2C_PAD_CTRL_TRIEN_SDA_MASK);
	k_sleep(K_USEC(100));
	/* Restore pads to input mode */
	sys_write32(config->pad_cntl, (drive_strength << RESET_UNIT_I2C_PAD_CNTL_DRV_SHIFT) |
					      RESET_UNIT_I2C_PAD_CNTL_RXEN_MASK |
					      RESET_UNIT_I2C_PAD_CNTL_TRIEN_MASK);
	/* Return control of pads to I2C controller */
	sys_write32(RESET_UNIT_I2C_CNTL_REG_ADDR, i2c_rst_cntl | BIT(config->id));
	/* Reenable I2C controller */
	sys_write32(ena_addr, 1);

	return 0;
}

static int i2c_tt_bh_init(const struct device *dev)
{
	const struct i2c_tt_bh_config *config = dev->config;

	if (!device_is_ready(config->dw_i2c_dev)) {
		LOG_ERR("DW I2C device not ready");
		return -ENODEV;
	}

	i2c_dw_register_recover_bus_cb(config->dw_i2c_dev, i2c_tt_bh_recover_bus, dev);

	return 0;
}

#define DEFINE_I2C_TT_BH(_num)                                                                     \
	static const struct i2c_tt_bh_config i2c_tt_bh_config_##_num = {                           \
		.dw_i2c_dev = DEVICE_DT_GET(DT_INST_PHANDLE(_num, dw_i2c_dev)),                    \
		.pad_cntl = DT_INST_PROP(_num, padcntl_reg),                                       \
		.pad_data = DT_INST_PROP(_num, paddata_reg),                                       \
		.id = (_num),                                                                      \
	};                                                                                         \
	I2C_DEVICE_DT_INST_DEFINE(_num, i2c_tt_bh_init, NULL, NULL, &i2c_tt_bh_config_##_num,      \
				  POST_KERNEL, CONFIG_I2C_TT_BH_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_I2C_TT_BH)
