/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TENSTORRENT_TT_BLACKHOLE_PINCTRL_SOC_H_
#define SOC_TENSTORRENT_TT_BLACKHOLE_PINCTRL_SOC_H_

#include <zephyr/dt-bindings/pinctrl/tt_blackhole_smc-pinctrl.h>
#include <zephyr/types.h>

#define PINCTRL_TT_BH_PINS          64
#define PINCTRL_TT_BH_PINS_PER_BANK 16

typedef struct pinctrl_soc_pin {
	uint32_t pin;
	uint32_t iofunc;
	uint32_t iomode;
} pinctrl_soc_pin_t;

/* Convert DT flags to SoC flags */
#define PINCTRL_TT_BH_DT_PIN_FLAGS(node_id)                                                        \
	(DT_PROP_OR(node_id, input_enable, 0) * PINCTRL_TT_BH_TRIEN |                              \
	 DT_PROP_OR(node_id, bias_pull_up, 0) * PINCTRL_TT_BH_PUEN |                               \
	 DT_PROP_OR(node_id, bias_pull_down, 0) * PINCTRL_TT_BH_PDEN |                             \
	 DT_PROP_OR(node_id, receive_enable, 0) * PINCTRL_TT_BH_RXEN |                             \
	 DT_PROP_OR(node_id, input_schmitt_enable, 0) * PINCTRL_TT_BH_STEN |                       \
	 PINCTRL_TT_BH_DRVS(DT_PROP_OR(node_id, drive_strength, PINCTRL_TT_BH_DRVS_DFLT)))

#define PINCTRL_TT_BH_DT_PIN(node_id)                                                              \
	{                                                                                          \
		.pin = DT_PROP_BY_IDX(node_id, pinmux, 0),                                         \
		.iofunc = DT_PROP_BY_IDX(node_id, pinmux, 1),                                      \
		.iomode = PINCTRL_TT_BH_DT_PIN_FLAGS(node_id),                                     \
	},

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	PINCTRL_TT_BH_DT_PIN(DT_PROP_BY_IDX(node_id, prop, idx))

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{                                                                                          \
		DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)                      \
	}

#endif
