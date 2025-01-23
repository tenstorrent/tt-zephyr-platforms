/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <app_version.h>
#include <tenstorrent/bist.h>
#include <tenstorrent/fan_ctrl.h>
#include <tenstorrent/fwupdate.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <tenstorrent/tt_smbus.h>
#include <tenstorrent/bh_chip.h>
#include <tenstorrent/bh_arc.h>
#include <tenstorrent/jtag_bootrom.h>

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

struct bh_chip BH_CHIPS[BH_CHIP_COUNT] = {DT_FOREACH_PROP_ELEM(DT_PATH(chips), chips, INIT_CHIP)};

#if BH_CHIP_PRIMARY_INDEX >= BH_CHIP_COUNT
#error "Primary chip out of range"
#endif

static const struct gpio_dt_spec board_fault_led =
	GPIO_DT_SPEC_GET_OR(DT_PATH(board_fault_led), gpios, {0});

int update_fw(void)
{
	/* To get here we are already running known good fw */
	int ret;

	const struct gpio_dt_spec reset_spi = BH_CHIPS[BH_CHIP_PRIMARY_INDEX].config.spi_reset;

	ret = gpio_pin_configure_dt(&reset_spi, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("%s() failed (could not configure the spi_reset pin): %d",
			"gpio_pin_configure_dt", ret);
		return 0;
	}

	gpio_pin_set_dt(&reset_spi, 1);
	k_busy_wait(1000);
	gpio_pin_set_dt(&reset_spi, 0);

	if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
		/* Check for and apply a new update, if one exists (we disable reboot here) */
		ret = tt_fwupdate("bmfw", false, false);
		if (ret < 0) {
			LOG_ERR("%s() failed: %d", "tt_fwupdate", ret);
			/*
			 * This might be as simple as no update being found, but it could be due to
			 * something else - e.g. I/O error, failure to read from external spi,
			 * failure to write to internal flash, image corruption / crc failure, etc.
			 */
			return 0;
		}

		if (ret == 0) {
			LOG_DBG("No firmware update required");
		} else {
			LOG_INF("Reboot needed in order to apply bmfw update");
			if (IS_ENABLED(CONFIG_REBOOT)) {
				sys_reboot(SYS_REBOOT_COLD);
			}
		}
	} else {
		ret = 0;
	}

	return ret;
}

void process_cm2bm_message(struct bh_chip *chip)
{
	cm2bmMessageRet msg = bh_chip_get_cm2bm_message(chip);

	if (msg.ret == 0) {
		cm2bmMessage message = msg.msg;

		switch (message.msg_id) {
		case 0x1:
			switch (message.data) {
			case 0x0:
				jtag_bootrom_reset_sequence(chip, true);
				break;
			case 0x3:
				/* Trigger reboot; will reset asic and reload bmfw
				 */
				if (IS_ENABLED(CONFIG_REBOOT)) {
					sys_reboot(SYS_REBOOT_COLD);
				}
				break;
			}
			break;
		case 0x2:
			/* Respond to ping request from CMFW */
			bharc_smbus_word_data_write(&chip->config.arc,
						    0x21, 0xA5A5);
			break;
		case 0x3:
			if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
				set_fan_speed((uint8_t)message.data & 0xFF);
			}
			break;
		}
	}
}

int main(void)
{
	int ret;
	int bist_rc;

	if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
		/* Only try to update from the primary chip spi */
		ret = tt_fwupdate_init(BH_CHIPS[BH_CHIP_PRIMARY_INDEX].config.flash,
				       BH_CHIPS[BH_CHIP_PRIMARY_INDEX].config.spi_mux);
		if (ret != 0) {
			return ret;
		}
	}

	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		if (chip->config.arc.smbus.bus == NULL) {
			continue;
		}

		tt_smbus_stm32_set_abort_ptr(chip->config.arc.smbus.bus,
					     &((&chip->data)->bus_cancel_flag));
	}

	bist_rc = 0;
	if (IS_ENABLED(CONFIG_TT_BIST)) {
		bist_rc = tt_bist();
		if (bist_rc < 0) {
			LOG_ERR("%s() failed: %d", "tt_bist", bist_rc);
		} else {
			LOG_DBG("Built-in self-test succeeded");
		}
	}

	if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
		ret = init_fan();
		if (ret != 0) {
			LOG_ERR("%s() failed: %d", "init_fan", ret);
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
		if (!tt_fwupdate_is_confirmed()) {
			if (bist_rc < 0) {
				LOG_ERR("Firmware update was unsuccessful and will be rolled-back "
					"after bmfw reboot.");
				if (IS_ENABLED(CONFIG_REBOOT)) {
					sys_reboot(SYS_REBOOT_COLD);
				}
				return EXIT_FAILURE;
			}

			ret = tt_fwupdate_confirm();
			if (ret < 0) {
				LOG_ERR("%s() failed: %d", "tt_fwupdate_confirm", ret);
				return EXIT_FAILURE;
			}
		}
	}

	ret = update_fw();
	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
		ret = tt_fwupdate_complete();
		if (ret != 0) {
			return ret;
		}
	}

	/* Force all spi_muxes back to arc control */
	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		if (chip->config.spi_mux.port != NULL) {
			gpio_pin_configure_dt(&chip->config.spi_mux, GPIO_OUTPUT_ACTIVE);
		}
	}

	if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
		gpio_pin_configure_dt(&board_fault_led, GPIO_OUTPUT_ACTIVE);
	}

	if (IS_ENABLED(CONFIG_JTAG_LOAD_BOOTROM)) {
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			ret = jtag_bootrom_init(chip);
			if (ret != 0) {
				LOG_ERR("%s() failed: %d", "jtag_bootrom_init", ret);
				return ret;
			}

			ret = jtag_bootrom_reset_sequence(chip, false);
			if (ret != 0) {
				LOG_ERR("%s() failed: %d", "jtag_bootrom_reset", ret);
				return ret;
			}
		}

		LOG_DBG("Bootrom workaround successfully applied");
	}

	LOG_DBG("BMFW VERSION " APP_VERSION_STRING "\n");

	/* No mechanism for getting bl version... yet */
	bmStaticInfo static_info =
		(bmStaticInfo){.version = 1, .bl_version = 0, .app_version = APPVERSION};

	if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
		gpio_pin_set_dt(&board_fault_led, 0);
	}

	while (1) {
		k_sleep(K_MSEC(20));

		if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
			/* Blink the light every half second or so */
			k_sleep(K_MSEC(500 - 20));
			gpio_pin_toggle_dt(&board_fault_led);
		}

		/* TODO(drosen): Turn this into a task which will re-arm until static data is sent
		 */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.arc_just_reset) {
				if (bh_chip_set_static_info(chip, &static_info) == 0) {
					chip->data.arc_just_reset = false;
				}
			}
		}

		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			process_cm2bm_message(chip);
		}

		/*
		 * Really only matters if running without security... but
		 * cm should register that it is on the pcie bus and therefore can be an update
		 * candidate. If chips that are on the bus see that an update has been requested
		 * they can update?
		 */
	}

	return EXIT_SUCCESS;
}
