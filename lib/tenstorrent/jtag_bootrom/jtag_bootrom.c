/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "blackhole_offsets.h"

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

static const struct device *JTAG = DEVICE_DT_GET(DT_PATH(jtag0));

bool jtag_axiwait(uint32_t addr)
{
	/* If we are using the emulated driver then always return true */
	if (DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)) {
		return true;
	}

	jtag_reset(JTAG);

	uint32_t value = 0;

	return !jtag_axi_read32(JTAG, addr, &value);
}

uint32_t jtag_bitbang_wait_for_id(void)
{
	uint32_t reset_id;

	do {
		jtag_reset(JTAG);
		jtag_read_id(JTAG, &reset_id);
	} while (reset_id != 0x138A5);

	return reset_id;
}

static const struct gpio_dt_spec reset_mcu = GPIO_DT_SPEC_GET(DT_ALIAS(reset_mcu), gpios);
static const struct gpio_dt_spec reset_spi = GPIO_DT_SPEC_GET(DT_ALIAS(reset_spi), gpios);
static const struct gpio_dt_spec reset_power = GPIO_DT_SPEC_GET(DT_ALIAS(reset_power), gpios);
static const struct gpio_dt_spec pgood = GPIO_DT_SPEC_GET(DT_ALIAS(pgood), gpios);

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
static const struct gpio_dt_spec preset_trigger = GPIO_DT_SPEC_GET(DT_ALIAS(preset_trigger), gpios);

static bool arc_reset;
static bool workaround_applied;
static bool reset_asap;
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
#endif /* IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET) */

int jtag_bootrom_setup(void)
{
	/* Only check for pgood if we aren't emulating */
#if !DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)
	while (!gpio_pin_get_dt(&pgood)) {
	}
#endif

	gpio_pin_set_dt(&reset_mcu, 1);
	gpio_pin_set_dt(&reset_spi, 1);

	int ret = jtag_setup(JTAG);

	if (ret) {
		/* LOG_ERR("Failed to initialize DAP controller, %d", ret); */
		return ret;
	}

	k_busy_wait(1000);

	gpio_pin_set_dt(&reset_spi, 0);
	gpio_pin_set_dt(&reset_mcu, 0);

	k_busy_wait(2000);

	jtag_reset(JTAG);

#if !DT_HAS_COMPAT_STATUS_OKAY(zephyr_gpio_emul)
	volatile uint32_t reset_id = jtag_bitbang_wait_for_id();

	if (reset_id != 0x138A5) {
		jtag_teardown(JTAG);
		if (reset_id == 0) {
			return -1;
		} else {
			return reset_id;
		}
	}
#endif

	jtag_reset(JTAG);

	while (!jtag_axiwait(BH_RESET_BASE + 0x60)) {
	}

	jtag_reset(JTAG);

	return 0;
}

int jtag_bootrom_init(void)
{
	int ret = 0;

	ret |= gpio_pin_configure_dt(&reset_mcu, GPIO_OUTPUT_ACTIVE) ||
	       gpio_pin_configure_dt(&reset_spi, GPIO_OUTPUT_ACTIVE) ||
	       gpio_pin_configure_dt(&reset_power, GPIO_INPUT) ||
	       gpio_pin_configure_dt(&pgood, GPIO_INPUT);
	if (ret) {
		return ret;
	}

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
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

	/* Active LOW, so will be false if high */
	if (!gpio_pin_get_dt(&preset_trigger)) {
		/* If the preset trigger started high, then we came out of reset with the system
		 * thinking that pcie is ready to go. We need to forcibly apply the workaround to
		 * ensure this remains true.
		 */
		reset_asap = true;
	}
#endif /* IS_ENABLED(CONFIG_JTAG_LOAD_ON_PRESET) */

	return 0;
}

int jtag_bootrom_patch_offset(const uint32_t *patch, size_t patch_len, const uint32_t start_addr)
{
#ifdef CONFIG_JTAG_LOAD_BOOTROM
	jtag_reset(JTAG);

	/* HALT THE ARC CORE!!!!! */
	uint32_t arc_misc_cntl = 0;

	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);

	arc_misc_cntl |= (0b1111 << 4);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);
	/* Reset it back to zero */
	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);
	arc_misc_cntl &= ~(0b1111 << 4);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);

	/* Enable gpio trien */
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x1A0, 0xff00);

	/* Write to postcode */
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x60, 0xF2);

	jtag_axi_block_write(JTAG, start_addr, patch, patch_len);

	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x60, 0xF3);

#ifdef CONFIG_JTAG_LOAD_ON_PRESET
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
		/* ICCM start addr is 0 */
		uint32_t readback = 0;
#ifdef CONFIG_JTAG_EMUL
		jtag_emul_axi_read32(JTAG, i * 4, &readback);
#else
		jtag_axi_read32(JTAG, i * 4, &readback);
#endif
		if (patch[i] != readback) {
			printk("Bootcode mismatch at %03x. expected: %08x actual: %08x "
			       "¯\\_(ツ)_/¯\n",
			       i * 4, patch[i], readback);

			jtag_axi_write32(JTAG, BH_RESET_BASE + 0x60, 0x6);

			return 1;
		}
	}

	printk("Bootcode write verified! \\o/\n");

	return 0;
}

void jtag_bootrom_soft_reset_arc(void)
{
#ifdef CONFIG_JTAG_LOAD_BOOTROM
	uint32_t arc_misc_cntl = 0;

	jtag_reset(JTAG);

	/* HALT THE ARC CORE!!!!! */
	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);

	arc_misc_cntl |= (0b1111 << 4);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);
	/* Reset it back to zero */
	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);
	arc_misc_cntl &= ~(0b1111 << 4);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);

	/* Write reset_vector (rom_memory[0]) */
	jtag_axi_write32(JTAG, BH_ROM_BASE, 0x84);

	/* Toggle soft-reset */
	/* ARC_MISC_CNTL.soft_reset (12th bit) */
	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);

	/* Set to 1 */
	arc_misc_cntl = arc_misc_cntl | (1 << 12);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);

	jtag_axi_read32(JTAG, BH_RESET_BASE + 0x100, &arc_misc_cntl);
	/* Set to 0 */
	arc_misc_cntl = arc_misc_cntl & ~(1 << 12);
	jtag_axi_write32(JTAG, BH_RESET_BASE + 0x100, arc_misc_cntl);
#ifdef CONFIG_JTAG_LOAD_ON_PRESET
	arc_reset = true;
#endif
#endif
}

void jtag_bootrom_teardown(void)
{
	/* Just one more for good luck */
	jtag_reset(JTAG);

	jtag_teardown(JTAG);
	/* should be possible to call jtag_teardown(), but the dt device does not seem to exposed */
	/* jtag_teardown(DEVICE_DT_GET_OR_NULL(DT_INST(0, zephyr_jtag_gpio))); */
}
