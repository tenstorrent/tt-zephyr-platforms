/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_blackhole_pinctrl

#include "pinctrl_soc.h"

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define PINCTRL_TT_BH_BASE_ADDR 0x80030000

#define PINCTRL_TT_BH_GPIO_PAD_TRIEN_CNTL_REG_OFFSET     0x000001A0
#define PINCTRL_TT_BH_GPIO_PAD_PUEN_CNTL_REG_OFFSET      0x000001A4
#define PINCTRL_TT_BH_GPIO_PAD_PDEN_CNTL_REG_OFFSET      0x000001A8
#define PINCTRL_TT_BH_GPIO_PAD_RXEN_CNTL_REG_OFFSET      0x000001AC
#define PINCTRL_TT_BH_GPIO_PAD_DRV_CNTL_LOW_REG_OFFSET   0x000001B0
#define PINCTRL_TT_BH_GPIO2_PAD_TRIEN_CNTL_REG_OFFSET    0x00000240
#define PINCTRL_TT_BH_GPIO2_PAD_PUEN_CNTL_REG_OFFSET     0x00000244
#define PINCTRL_TT_BH_GPIO2_PAD_PDEN_CNTL_REG_OFFSET     0x00000248
#define PINCTRL_TT_BH_GPIO_PAD_DRV_CNTL_HIGH_REG_OFFSET  0x00000250
#define PINCTRL_TT_BH_GPIO2_PAD_RXEN_CNTL_REG_OFFSET     0x0000025C
#define PINCTRL_TT_BH_GPIO2_PAD_DRV_CNTL_LOW_REG_OFFSET  0x00000278
#define PINCTRL_TT_BH_GPIO2_PAD_DRV_CNTL_HIGH_REG_OFFSET 0x0000027C
#define PINCTRL_TT_BH_GPIO3_PAD_TRIEN_CNTL_REG_OFFSET    0x00000580
#define PINCTRL_TT_BH_GPIO3_PAD_PUEN_CNTL_REG_OFFSET     0x00000584
#define PINCTRL_TT_BH_GPIO3_PAD_PDEN_CNTL_REG_OFFSET     0x00000588
#define PINCTRL_TT_BH_GPIO3_PAD_RXEN_CNTL_REG_OFFSET     0x0000058C
#define PINCTRL_TT_BH_GPIO3_PAD_DRV_CNTL_LOW_REG_OFFSET  0x00000590
#define PINCTRL_TT_BH_GPIO4_PAD_PUEN_CNTL_REG_OFFSET     0x000005A4
#define PINCTRL_TT_BH_GPIO4_PAD_PDEN_CNTL_REG_OFFSET     0x000005A8
#define PINCTRL_TT_BH_GPIO4_PAD_TRIEN_CNTL_REG_OFFSET    0x000005A0
#define PINCTRL_TT_BH_GPIO4_PAD_RXEN_CNTL_REG_OFFSET     0x000005AC
#define PINCTRL_TT_BH_GPIO3_PAD_DRV_CNTL_HIGH_REG_OFFSET 0x000005B0
#define PINCTRL_TT_BH_GPIO4_PAD_DRV_CNTL_LOW_REG_OFFSET  0x000005BC
#define PINCTRL_TT_BH_GPIO4_PAD_DRV_CNTL_HIGH_REG_OFFSET 0x000005C0
#define PINCTRL_TT_BH_GPIO_PAD_STEN_CNTL_REG_OFFSET      0x000005F0
#define PINCTRL_TT_BH_GPIO2_PAD_STEN_CNTL_REG_OFFSET     0x000005F4
#define PINCTRL_TT_BH_GPIO3_PAD_STEN_CNTL_REG_OFFSET     0x000005F8
#define PINCTRL_TT_BH_GPIO4_PAD_STEN_CNTL_REG_OFFSET     0x000005FC

#define PINCTRL_TT_BH_UART_CNTL_REG_OFFSET 0x00000608

LOG_MODULE_REGISTER(bh_arc_pinctrl, CONFIG_PINCTRL_LOG_LEVEL);

static inline uint32_t pinctrl_tt_bh_pin_to_bank(uint32_t pin);
static inline uint32_t pinctrl_tt_bh_pin_to_idx(uint32_t pin);

static inline uintptr_t pinctrl_tt_bh_trien_reg(uint32_t pin);
static inline uintptr_t pinctrl_tt_bh_puen_reg(uint32_t pin);
static inline uintptr_t pinctrl_tt_bh_pden_reg(uint32_t pin);
static inline uintptr_t pinctrl_tt_bh_rxen_reg(uint32_t pin);
static inline uintptr_t pinctrl_tt_bh_sten_reg(uint32_t pin);

static inline uintptr_t pinctrl_tt_bh_drvs_reg(uint32_t pin);
static inline uint32_t pinctrl_tt_bh_drvs_shift(uint32_t pin);

static int pinctrl_tt_bh_set(uint32_t pin, uint32_t func, uint32_t mode)
{
	uint32_t idx;

	if (pin >= PINCTRL_TT_BH_PINS) {
		return -EINVAL;
	}

	if (func > 1) {
		return -EINVAL;
	}

	if (func == 0) {
		/* GPIO only */
		return 0;
	}

	/* Assumes only 1 alternate function per pin */
	switch (pin) {
	case 48: /* uart0_tx_default */
	case 49: /* uart0_rx_default */
		break;
	case 0xfffffffe: /* fake pin for i2c0 scl */
		break;
	case 0xfffffffd: /* fake pin for i2c0 sda */
		break;
	case 0xfffffffc: /* fake pin for i2c1 scl */
		break;
	case 0xfffffffb: /* fake pin for i2c1 sda */
		break;
	case 0xfffffffa: /* fake pin for i2c2 scl */
		break;
	case 0xfffffff9: /* fake pin for i2c2 sda */
		break;
	default:
		LOG_DBG("No alternate function for pin %u", pin);
		return -EIO;
	}

	idx = pinctrl_tt_bh_pin_to_bank(pin);

	/* input-enable */
	if ((mode & PINCTRL_TT_BH_TRIEN) != 0) {
		sys_write32(pinctrl_tt_bh_trien_reg(pin), BIT(idx));

		/* input-schmitt-enable */
		if ((mode & PINCTRL_TT_BH_STEN) != 0) {
			sys_write32(pinctrl_tt_bh_sten_reg(pin), BIT(idx));
		}
	}

	/* bias-pull-up */
	if ((mode & PINCTRL_TT_BH_PUEN) != 0) {
		sys_write32(pinctrl_tt_bh_puen_reg(pin), BIT(idx));
	} else if ((mode & PINCTRL_TT_BH_PDEN) != 0) {
		/* bias-pull-down */
		sys_write32(pinctrl_tt_bh_pden_reg(pin), BIT(idx));
	}

	/* receive-enable */
	if ((mode & PINCTRL_TT_BH_RXEN) != 0) {
		sys_write32(pinctrl_tt_bh_rxen_reg(pin), BIT(idx));
	}

	/* drive-strength */
	if (PINCTRL_TT_BH_DRVS(mode) != PINCTRL_TT_BH_DRVS_DFLT) {
		sys_write32(pinctrl_tt_bh_drvs_reg(pin), PINCTRL_TT_BH_DRVS(mode)
								 << pinctrl_tt_bh_drvs_shift(pin));
	}

	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	ARG_UNUSED(reg);

	int ret;

	for (uint8_t i = 0; i < pin_cnt; i++) {
		ret = pinctrl_tt_bh_set(pins[i].pin, pins[i].iofunc, pins[i].iomode);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static inline uint32_t pinctrl_tt_bh_pin_to_bank(uint32_t pin)
{
	return pin >> LOG2(PINCTRL_TT_BH_PINS_PER_BANK);
}

static inline uint32_t pinctrl_tt_bh_pin_to_idx(uint32_t pin)
{
	return pin & BIT_MASK(PINCTRL_TT_BH_PINS_PER_BANK);
}

static inline uintptr_t pinctrl_tt_bh_trien_reg(uint32_t pin)
{
	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO_PAD_TRIEN_CNTL_REG_OFFSET;
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO2_PAD_TRIEN_CNTL_REG_OFFSET;
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO3_PAD_TRIEN_CNTL_REG_OFFSET;
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO4_PAD_TRIEN_CNTL_REG_OFFSET;
	default:
		CODE_UNREACHABLE;
	}
}

static inline uintptr_t pinctrl_tt_bh_puen_reg(uint32_t pin)
{
	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO_PAD_PUEN_CNTL_REG_OFFSET;
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO2_PAD_PUEN_CNTL_REG_OFFSET;
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO3_PAD_PUEN_CNTL_REG_OFFSET;
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO4_PAD_PUEN_CNTL_REG_OFFSET;
	default:
		CODE_UNREACHABLE;
	}
}

static inline uintptr_t pinctrl_tt_bh_pden_reg(uint32_t pin)
{
	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO_PAD_PDEN_CNTL_REG_OFFSET;
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO2_PAD_PDEN_CNTL_REG_OFFSET;
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO3_PAD_PDEN_CNTL_REG_OFFSET;
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO4_PAD_PDEN_CNTL_REG_OFFSET;
	default:
		CODE_UNREACHABLE;
	}
}

static inline uintptr_t pinctrl_tt_bh_rxen_reg(uint32_t pin)
{
	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO_PAD_RXEN_CNTL_REG_OFFSET;
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO2_PAD_RXEN_CNTL_REG_OFFSET;
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO3_PAD_RXEN_CNTL_REG_OFFSET;
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO4_PAD_RXEN_CNTL_REG_OFFSET;
	default:
		CODE_UNREACHABLE;
	}
}

static inline uintptr_t pinctrl_tt_bh_sten_reg(uint32_t pin)
{
	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO_PAD_STEN_CNTL_REG_OFFSET;
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO2_PAD_STEN_CNTL_REG_OFFSET;
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO3_PAD_STEN_CNTL_REG_OFFSET;
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR + PINCTRL_TT_BH_GPIO4_PAD_STEN_CNTL_REG_OFFSET;
	default:
		CODE_UNREACHABLE;
	}
}

static inline uintptr_t pinctrl_tt_bh_drvs_reg(uint32_t pin)
{
	bool hi = (pin & PINCTRL_TT_BH_DRVS_MAX) != 0;

	switch (pinctrl_tt_bh_pin_to_bank(pin)) {
	case 0:
		return PINCTRL_TT_BH_BASE_ADDR +
		       (hi ? PINCTRL_TT_BH_GPIO_PAD_DRV_CNTL_HIGH_REG_OFFSET
			   : PINCTRL_TT_BH_GPIO_PAD_DRV_CNTL_LOW_REG_OFFSET);
	case 1:
		return PINCTRL_TT_BH_BASE_ADDR +
		       (hi ? PINCTRL_TT_BH_GPIO2_PAD_DRV_CNTL_HIGH_REG_OFFSET
			   : PINCTRL_TT_BH_GPIO2_PAD_DRV_CNTL_LOW_REG_OFFSET);
	case 2:
		return PINCTRL_TT_BH_BASE_ADDR +
		       (hi ? PINCTRL_TT_BH_GPIO3_PAD_DRV_CNTL_HIGH_REG_OFFSET
			   : PINCTRL_TT_BH_GPIO3_PAD_DRV_CNTL_LOW_REG_OFFSET);
	case 3:
		return PINCTRL_TT_BH_BASE_ADDR +
		       (hi ? PINCTRL_TT_BH_GPIO4_PAD_DRV_CNTL_HIGH_REG_OFFSET
			   : PINCTRL_TT_BH_GPIO4_PAD_DRV_CNTL_LOW_REG_OFFSET);
	default:
		CODE_UNREACHABLE;
	}
}

static inline uint32_t pinctrl_tt_bh_drvs_shift(uint32_t pin)
{
	return (pin * PINCTRL_TT_BH_DRVS_BITS) & BIT_MASK(LOG2(32));
}
