/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_LIB_BH_CHIP_H_
#define INCLUDE_TENSTORRENT_LIB_BH_CHIP_H_

#include "bh_arc.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct bh_straps {
	struct gpio_dt_spec gpio6;
};

struct bh_chip_config {
	struct gpio_dt_spec asic_reset;
	struct gpio_dt_spec spi_reset;
	struct gpio_dt_spec spi_mux;
	struct gpio_dt_spec pgood;
	struct gpio_dt_spec therm_trip;
	const struct device *flash;
	const struct device *jtag;

	const struct bh_straps strapping;

	struct bh_arc arc;
};

struct bh_chip_data {
	/* Flag set when we need to apply the reset regardless of preset state. */
	bool needs_reset;

	/* Flag set when bootrom has been loaded and the arc_soft_reset sequence can be appled. */
	bool workaround_applied;

	/*
	 * Flag set when need to send or receive 1 time info to chip.
	 * Could be used for static data or config of peripherals.
	 */
	bool arc_needs_init_msg;

	unsigned int bus_cancel_flag;

	/* notify the main thread to apply reset sequence */
	bool trigger_reset;

	/* notify the main thread to handle therm trip */
	volatile bool therm_trip_triggered;

	/* notify the main thread to handle pgood events */
	volatile bool pgood_fall_triggered;
	volatile bool pgood_rise_triggered;
	bool pgood_severe_fault;
	int64_t pgood_last_trip_ms;

	/* Max allowable time between pings from SMC in ms */
	uint32_t auto_reset_timeout;

	/* Keep track of telemetry heartbeat for autoreset */
	uint32_t telemetry_heartbeat;

	/* Flag set when last reset was an autoreset */
	bool last_reset_was_automatic;
};

struct bh_chip {
	const struct bh_chip_config config;
	struct bh_chip_data data;
	struct gpio_callback therm_trip_cb;
	struct gpio_callback pgood_cb;
	struct k_timer auto_reset_timer;
};

#define DT_PHANDLE_OR_CHILD(node_id, name)                                                         \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, name), (DT_PHANDLE(node_id, name)),                 \
	      (DT_CHILD(node_id, name)))

#define HAS_DT_PHANDLE_OR_CHILD(node_id, name) DT_NODE_EXISTS(DT_PHANDLE_OR_CHILD(node_id, name))
#define BH_CHIP_COUNT                          DT_PROP_LEN_OR(DT_PATH(chips), chips, 0)
extern struct bh_chip BH_CHIPS[BH_CHIP_COUNT];

#define MAKE_STRUCT_FIELD(n) .n
#define INIT_STRAP(n)        MAKE_STRUCT_FIELD(DT_NODE_FULL_NAME_TOKEN(n)) = \
	GPIO_DT_SPEC_GET(n, gpios),

#define INIT_CHIP(n, prop, idx)                                                                    \
	{                                                                                          \
		.config = {                                                                        \
			.asic_reset = GPIO_DT_SPEC_GET(                                            \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), asic_reset),  \
				gpios),                                                            \
			.spi_reset = GPIO_DT_SPEC_GET(                                             \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), spi_reset),   \
				gpios),                                                            \
			.flash = DEVICE_DT_GET_OR_NULL(                                            \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), flash)),      \
			.jtag = DEVICE_DT_GET(                                                     \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), jtag)),       \
			.arc = BH_ARC_INIT(                                                        \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), arc)),        \
			.spi_mux = GPIO_DT_SPEC_GET(                                               \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), spi_mux),     \
				gpios),                                                            \
			.pgood = GPIO_DT_SPEC_GET(                                                 \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), pgood),       \
				gpios),                                                            \
			.therm_trip = GPIO_DT_SPEC_GET(                                            \
				DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), therm_trip),  \
				gpios),                                                            \
			.strapping = {COND_CODE_1(                                                 \
	  HAS_DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), strapping),                     \
	  (DT_FOREACH_CHILD(DT_PHANDLE_OR_CHILD(DT_PHANDLE_BY_IDX(n, prop, idx), strapping),       \
			    INIT_STRAP)),                                                          \
	  ())},                                            \
			},                                                                         \
				.auto_reset_timer = Z_TIMER_INITIALIZER(                           \
					BH_CHIPS[idx].auto_reset_timer, bh_chip_auto_reset, NULL), \
			},

#define BH_CHIP_PRIMARY_INDEX DT_PROP(DT_PATH(chips), primary)

int jtag_bootrom_reset_sequence(struct bh_chip *chip, bool force_reset);

void bh_chip_cancel_bus_transfer_set(struct bh_chip *chip);
void bh_chip_cancel_bus_transfer_clear(struct bh_chip *chip);

cm2dmMessageRet bh_chip_get_cm2dm_message(struct bh_chip *chip);
int bh_chip_set_static_info(struct bh_chip *chip, dmStaticInfo *info);
int bh_chip_set_input_power(struct bh_chip *chip, uint16_t power);
int bh_chip_set_input_power_lim(struct bh_chip *chip, uint16_t max_power);
int bh_chip_set_fan_rpm(struct bh_chip *chip, uint16_t rpm);

void bh_chip_auto_reset(struct k_timer *timer);

void bh_chip_assert_asic_reset(const struct bh_chip *chip);
void bh_chip_deassert_asic_reset(const struct bh_chip *chip);

void bh_chip_set_straps(struct bh_chip *chip);
void bh_chip_unset_straps(struct bh_chip *chip);

void bh_chip_assert_spi_reset(const struct bh_chip *chip);
void bh_chip_deassert_spi_reset(const struct bh_chip *chip);

int bh_chip_reset_chip(struct bh_chip *chip, bool force_reset);

int therm_trip_gpio_setup(struct bh_chip *chip);
int pgood_gpio_setup(struct bh_chip *chip);

void handle_pgood_event(struct bh_chip *chip, struct gpio_dt_spec board_fault_led);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_TENSTORRENT_LIB_BH_CHIP_H_ */
