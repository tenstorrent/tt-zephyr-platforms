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
	struct k_mutex reset_lock;

	/* Flag set when we need to apply the reset regardless of preset state. */
	bool needs_reset;

	/* Flag set when bootrom has been loaded and the arc_soft_reset sequence can be appled. */
	bool workaround_applied;

	/*
	 * Flag set when need to send or receive 1 time info to chip.
	 * Could be used for static data or config of peripherals.
	 */
	bool arc_just_reset;

	unsigned int bus_cancel_flag;
};

struct bh_chip {
	const struct bh_chip_config config;
	struct bh_chip_data data;
	struct gpio_callback therm_trip_cb;
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
	  ())},                                    \
				},                                                                 \
				.data =                                                            \
					{                                                          \
						.reset_lock = Z_MUTEX_INITIALIZER(                 \
							BH_CHIPS[idx].data.reset_lock),            \
					},                                                         \
			},

#define BH_CHIP_PRIMARY_INDEX DT_PROP(DT_PATH(chips), primary)

int jtag_bootrom_reset_sequence(struct bh_chip *chip, bool force_reset);

void bh_chip_cancel_bus_transfer_set(struct bh_chip *chip);
void bh_chip_cancel_bus_transfer_clear(struct bh_chip *chip);

cm2bmMessageRet bh_chip_get_cm2bm_message(struct bh_chip *chip);
int bh_chip_set_static_info(struct bh_chip *chip, bmStaticInfo *info);
int bh_chip_set_input_current(struct bh_chip *chip, int32_t *current);
int bh_chip_set_fan_rpm(struct bh_chip *chip, uint16_t rpm);
int bh_chip_set_board_pwr_lim(struct bh_chip *chip, uint16_t max_pwr);

void bh_chip_assert_asic_reset(const struct bh_chip *chip);
void bh_chip_deassert_asic_reset(const struct bh_chip *chip);

void bh_chip_set_straps(struct bh_chip *chip);
void bh_chip_unset_straps(struct bh_chip *chip);

void bh_chip_assert_spi_reset(const struct bh_chip *chip);
void bh_chip_deassert_spi_reset(const struct bh_chip *chip);

int bh_chip_reset_chip(struct bh_chip *chip, bool force_reset);

int therm_trip_gpio_setup(struct bh_chip *chip);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_TENSTORRENT_LIB_BH_CHIP_H_ */
