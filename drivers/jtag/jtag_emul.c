/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "axi.h"
#include "jtag_priv.h"

#include <stdbool.h>
#include <string.h>

#include <tenstorrent/bitrev.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#define REG_BITS 32

LOG_MODULE_REGISTER(jtag_emul, CONFIG_JTAG_LOG_LEVEL);

static const enum jtag_state next_state[2][16] = {
	/* TMS low */
	[0] = {

			[JTAG_RESET] = IDLE,
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
	[1] = {

			[JTAG_RESET] = JTAG_RESET,
			[IDLE] = SCAN_DR,
			[SCAN_DR] = SCAN_IR,
			[SCAN_IR] = JTAG_RESET,
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

__maybe_unused static const char *const jtag_state_to_str[] = {
	"RESET  ", "IDLE   ", "SCAN_DR", "SCAN_IR",  "CAPT_DR ", "CAPT_IR", "SHFT_DR", "SHFT_IR",
	"EXT1_DR", "EXT1_IR", "PAUS_DR", "PAUSE_IR", "EXT2_DR",  "EXT2_IR", "UPDT_DR", "UPDT_IR",
};

static void on_tck_falling(struct jtag_data *data, bool tdi);

static inline bool tck(struct jtag_data *data);
static inline bool tdi(struct jtag_data *data);
static inline bool tms(struct jtag_data *data);
static inline bool trst(struct jtag_data *data);

static void gpio_emul_callback(const struct device *port, struct gpio_callback *cb,
			       gpio_port_pins_t pins)
{
	struct jtag_data *data = CONTAINER_OF(cb, struct jtag_data, gpio_emul_cb);
	struct jtag_emul_data *edata = &data->emul_data;

	/* This function should _only_ be called when the TCK pin changes */
	__ASSERT_NO_MSG(pins & BIT(data->tck.pin));

	bool _tck = tck(data);
	bool _tms = tms(data);
	bool _tdi = tdi(data);

	if (_tck != edata->tck_old) {
		edata->tck_old = _tck;

		if (!_tck) {
			on_tck_falling(data, _tdi);
			edata->state = next_state[_tms][edata->state];

			/*
			 * LOG_DBG("%5zu\t%u\t%u\t%s\t%x\t%x\t%x\t%x", edata->tck_count, _tms, _tdi,
			 * jtag_state_to_str[edata->state],
			 *    bitrev32(edata->shift_reg[IR]) >> (32 - edata->shift_bits[IR] -
			 *    1), edata->shift_reg[DR], edata->hold_reg[IR],
			 *    edata->hold_reg[DR]);
			 */

			++edata->tck_count;
		}
	}
}

static void on_update_reg(struct jtag_data *data)
{
	struct jtag_emul_data *edata = &data->emul_data;

	switch (edata->selected_reg) {
	case DR: {
		edata->shift_bits[DR] = CLAMP(edata->shift_bits[DR], 1, REG_BITS);
		edata->hold_reg[DR] =
			bitrev32(edata->shift_reg[DR]) >> (REG_BITS - edata->shift_bits[DR]);

		if (edata->hold_reg[DR] - 1 == ARC_AXI_ADDR_TDR) {
			edata->have_axi_addr_tdr = true;
		} else if (edata->have_axi_addr_tdr) {
			edata->have_axi_addr_tdr = false;
			edata->axi_addr_tdr = edata->hold_reg[DR];
		} else if (edata->hold_reg[DR] - 1 == ARC_AXI_DATA_TDR) {
			edata->have_axi_data_tdr = true;
		} else if (edata->have_axi_data_tdr) {
			edata->have_axi_data_tdr = false;
			edata->axi_data_tdr = edata->hold_reg[DR];

			size_t i = edata->axi_addr_tdr >> LOG2(sizeof(uint32_t));

			if (i < data->buf_len) {
				data->buf[i] = edata->axi_data_tdr;
				LOG_DBG("W: addr: %03x data: %08x", edata->axi_addr_tdr,
					edata->axi_data_tdr);
			}
		}
	} break;
	case IR:
		edata->hold_reg[IR] =
			bitrev32(edata->shift_reg[IR]) >> (REG_BITS - edata->shift_bits[IR] - 1);
		break;
	default:
		break;
	}
}

/* _tms is the "incoming"" _tms value w.r.t. the state diagram */
/* we only take action here based on the "incoming" TMS value and not the outcoing */
static void on_tck_falling(struct jtag_data *data, bool _tdi)
{
	struct jtag_emul_data *edata = &data->emul_data;

	switch (edata->state) {
	case SCAN_DR:
	case SCAN_IR:
		edata->selected_reg = (edata->state == SCAN_DR) ? DR : IR;
		break;
	case CAPTURE_DR:
	case CAPTURE_IR:
		edata->shift_bits[edata->selected_reg] = 0;
		break;
	case SHIFT_DR:
	case SHIFT_IR:
		if (!tms(data)) {
			edata->shift_reg[edata->selected_reg] <<= 1;
			edata->shift_reg[edata->selected_reg] |= _tdi;
			++edata->shift_bits[edata->selected_reg];
		}
		break;
	case UPDATE_DR:
	case UPDATE_IR:
		on_update_reg(data);
		break;
	default:
		break;
	}
}

static inline bool tck(struct jtag_data *data)
{
	return gpio_emul_output_get(data->tck.port, data->tck.pin);
}

static inline bool tdi(struct jtag_data *data)
{
	return gpio_emul_output_get(data->tdi.port, data->tdi.pin);
}

static inline bool tms(struct jtag_data *data)
{
	return gpio_emul_output_get(data->tms.port, data->tms.pin);
}

static inline bool trst(struct jtag_data *data)
{
	return gpio_emul_output_get(data->trst.port, data->trst.pin);
}

void jtag_emul_setup(const struct device *dev, uint32_t *buf, size_t buf_len)
{
	const struct jtag_data *cfg = dev->config;
	struct jtag_data *data = dev->data;

	data->buf = buf;
	data->buf_len = buf_len;

	data->tck = cfg->tck;
	data->tdi = cfg->tdi;
	data->tms = cfg->tms;
	data->trst = cfg->trst;

	data->emul_data = (struct jtag_emul_data){
		.state = IDLE,
		.selected_reg = BR,
		.tck_old = true,
	};

	gpio_init_callback(&data->gpio_emul_cb, gpio_emul_callback, BIT(cfg->tck.pin));
	gpio_add_callback(cfg->tck.port, &data->gpio_emul_cb);
}

int jtag_emul_axi_read32(const struct device *dev, uint32_t addr, uint32_t *value)
{
	struct jtag_data *data = dev->data;

	size_t idx = addr >> LOG2(sizeof(uint32_t));

	if (idx >= data->buf_len) {
		LOG_ERR("Invalid address %08x", addr);
		return -EINVAL;
	}

	LOG_DBG("R: addr: %03x data: %08x", addr, data->buf[idx]);
	*value = data->buf[idx];

	return 0;
}
