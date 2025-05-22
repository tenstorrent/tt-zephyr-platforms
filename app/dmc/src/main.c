/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdlib.h>

#include <app_version.h>
#include <tenstorrent/bist.h>
#include <tenstorrent/fan_ctrl.h>
#include <tenstorrent/fwupdate.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <tenstorrent/tt_smbus.h>
#include <tenstorrent/bh_chip.h>
#include <tenstorrent/bh_arc.h>
#include <tenstorrent/event.h>
#include <tenstorrent/jtag_bootrom.h>
#include <tenstorrent/tt_smbus_regs.h>

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

struct bh_chip BH_CHIPS[BH_CHIP_COUNT] = {DT_FOREACH_PROP_ELEM(DT_PATH(chips), chips, INIT_CHIP)};

#if BH_CHIP_PRIMARY_INDEX >= BH_CHIP_COUNT
#error "Primary chip out of range"
#endif

static const struct gpio_dt_spec board_fault_led =
	GPIO_DT_SPEC_GET_OR(DT_PATH(board_fault_led), gpios, {0});
static const struct device *const ina228 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina228));

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
		/*
		 * Check for and apply a new update, if one exists (we disable reboot here)
		 * Device Mgmt FW (called bmfw here and elsewhere in this file for historical
		 * reasons)
		 */
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
			LOG_INF("Reboot needed in order to apply dmfw update");
			if (IS_ENABLED(CONFIG_REBOOT)) {
				sys_reboot(SYS_REBOOT_COLD);
			}
		}
	} else {
		ret = 0;
	}

	return ret;
}

void process_cm2dm_message(struct bh_chip *chip)
{
	cm2dmMessageRet msg = bh_chip_get_cm2dm_message(chip);

	if (msg.ret == 0) {
		cm2dmMessage message = msg.msg;

		switch (message.msg_id) {
		case kCm2DmMsgIdResetReq:
			switch (message.data) {
			case 0x0:
				jtag_bootrom_reset_sequence(chip, true);
				break;
			case 0x3:
				/* Trigger reboot; will reset asic and reload dmfw
				 */
				if (IS_ENABLED(CONFIG_REBOOT)) {
					sys_reboot(SYS_REBOOT_COLD);
				}
				break;
			}
			break;
		case kCm2DmMsgIdPing:
			/* Respond to ping request from CMFW */
			bharc_smbus_word_data_write(&chip->config.arc, CMFW_SMBUS_PING, 0xA5A5);
			break;
		case kCm2DmMsgIdFanSpeedUpdate:
			if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
				set_fan_speed((uint8_t)message.data & 0xFF);
			}
			break;
		case kCm2DmMsgIdReady:
			chip->data.arc_needs_init_msg = true;
			break;
		}
	}
}

void ina228_power_update(void)
{
	struct sensor_value sensor_val;

	sensor_sample_fetch_chan(ina228, SENSOR_CHAN_POWER);
	sensor_channel_get(ina228, SENSOR_CHAN_POWER, &sensor_val);

	/* Only use integer part of sensor value */
	int16_t power = sensor_val.val1 & 0xFFFF;

	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		bh_chip_set_input_power(chip, power);
	}
}

#if 0
static void ina228_repeat_test(void)
{
	/* loop until we detect an change in ina228 current.
	 * record current time
	 * loop until we detect another change in ina228 current.
	 * compute elapsed time, update min time between changes
	 * repeat 5x
	 */

	bh_chip_debug_data(&BH_CHIPS[0], 58, 0xFFFFFFFF);

	struct sensor_value c1, c2;

	sensor_sample_fetch_chan(ina228, SENSOR_CHAN_CURRENT);
	sensor_channel_get(ina228, SENSOR_CHAN_CURRENT, &c1);

	uint32_t emergency_break = 0;

	while (true) {
		sensor_sample_fetch_chan(ina228, SENSOR_CHAN_CURRENT);
		sensor_channel_get(ina228, SENSOR_CHAN_CURRENT, &c2);

		if (c1.val2 != c2.val2 || c1.val1 != c2.val1) {
			break;
		}

		if (++emergency_break >= 100000) return;
	}

	/* okay we just saw an edge, latest reading in c2 */

	uint64_t shortest_time_so_far = UINT64_MAX;

	for (unsigned int i = 0; i < 5; i++) {
		emergency_break = 0;
		uint64_t start_time = k_cycle_get_64();
		while (true) {
			sensor_sample_fetch_chan(ina228, SENSOR_CHAN_CURRENT);
			sensor_channel_get(ina228, SENSOR_CHAN_CURRENT, &c1);

			if (c1.val2 != c2.val2 || c1.val1 != c2.val1) {
				break;
			}

			if (++emergency_break >= 100000) return;
		}
		uint64_t elapsed_time = k_cycle_get_64() - start_time;
		/* now elapsed_time is edge-to-edge */

		if (elapsed_time < shortest_time_so_far)
			shortest_time_so_far = elapsed_time;

		c2 = c1;
	}

	bh_chip_debug_data(&BH_CHIPS[0], 58, shortest_time_so_far);
}
#endif

uint16_t detect_max_power(void)
{
	static const struct gpio_dt_spec psu_sense0 =
		GPIO_DT_SPEC_GET_OR(DT_PATH(psu_sense0), gpios, {0});
	static const struct gpio_dt_spec psu_sense1 =
		GPIO_DT_SPEC_GET_OR(DT_PATH(psu_sense1), gpios, {0});

	gpio_pin_configure_dt(&psu_sense0, GPIO_INPUT);
	gpio_pin_configure_dt(&psu_sense1, GPIO_INPUT);

	int sense0_val = gpio_pin_get_dt(&psu_sense0);
	int sense1_val = gpio_pin_get_dt(&psu_sense1);

	uint16_t psu_power;

	if (!sense0_val && !sense1_val) {
		psu_power = 600;
	} else if (sense0_val && !sense1_val) {
		psu_power = 450;
	} else if (!sense0_val && sense1_val) {
		psu_power = 300;
	} else {
		/* Pins could either be open or shorted together */
		/* Pull down one and check the other */
		gpio_pin_configure_dt(&psu_sense0, GPIO_OUTPUT_LOW);
		if (!gpio_pin_get_dt(&psu_sense1)) {
			/* If shorted together then max power is 150W */
			psu_power = 150;
		} else {
			psu_power = 0;
		}
		gpio_pin_configure_dt(&psu_sense0, GPIO_INPUT);
	}

	return psu_power;
}

/*
 * Runs a series of SMBUS tests when `CONFIG_DMC_RUN_SMBUS_TESTS` is enabled.
 * These tests aren't intended to be run on production firmware.
 */
static int bh_chip_run_smbus_tests(struct bh_chip *chip)
{
#ifdef CONFIG_DMC_RUN_SMBUS_TESTS
	int ret;
	int pass_val = 0xFEEDFACE;
	uint8_t count;
	uint8_t data[32]; /* Max size of SMBUS block read */

	/* Test SMBUS telemetry by selecting TAG_DM_APP_FW_VERSION and reading it back */
	ret = bharc_smbus_byte_data_write(&chip->config.arc, 0x26, 26);
	if (ret < 0) {
		LOG_ERR("Failed to write to SMBUS telemetry register");
		return ret;
	}
	ret = bharc_smbus_block_read(&chip->config.arc, 0x27, &count, data);
	if (ret < 0) {
		LOG_ERR("Failed to read from SMBUS telemetry register");
		return ret;
	}
	if (count != 4) {
		LOG_ERR("SMBUS telemetry read returned unexpected count: %d", count);
		return -EIO;
	}
	if ((*(uint32_t *)data) != APPVERSION) {
		LOG_ERR("SMBUS telemetry read returned unexpected value: %08x", *(uint32_t *)data);
		return -EIO;
	}

	/* Record test status into scratch register */
	ret = bharc_smbus_block_write(&chip->config.arc, 0xDD, sizeof(pass_val),
				      (uint8_t *)&pass_val);
	if (ret < 0) {
		LOG_ERR("Failed to write to SMBUS scratch register");
		return ret;
	}
	printk("SMBUS tests passed\n");
#endif
	return 0;
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

#if CONFIG_TT_SMBUS_DRIVER
	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		if (chip->config.arc.smbus.bus == NULL) {
			continue;
		}

		tt_smbus_stm32_set_abort_ptr(chip->config.arc.smbus.bus,
					     &((&chip->data)->bus_cancel_flag));
	}
#endif

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
		set_fan_speed(100); /* Set fan speed to 100 by default */
		if (ret != 0) {
			LOG_ERR("%s() failed: %d", "init_fan", ret);
			return ret;
		}
	}

	if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
		if (!tt_fwupdate_is_confirmed()) {
			if (bist_rc < 0) {
				LOG_ERR("Firmware update was unsuccessful and will be rolled-back "
					"after dmfw reboot.");
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

	/* Set up GPIOs */
	if (board_fault_led.port != NULL) {
		gpio_pin_configure_dt(&board_fault_led, GPIO_OUTPUT_INACTIVE);
	}

	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		ret = therm_trip_gpio_setup(chip);
		if (ret != 0) {
			LOG_ERR("%s() failed: %d", "therm_trip_gpio_setup", ret);
			return ret;
		}
		ret = pgood_gpio_setup(chip);
		if (ret != 0) {
			LOG_ERR("%s() failed: %d", "pgood_gpio_setup", ret);
			return ret;
		}
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

	printk("DMFW VERSION " APP_VERSION_STRING "\n");

	if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
		gpio_pin_set_dt(&board_fault_led, 1);
	}

	/* No mechanism for getting bl version... yet */
	dmStaticInfo static_info =
		(dmStaticInfo){.version = 1, .bl_version = 0, .app_version = APPVERSION};

	uint16_t max_power = detect_max_power();

	/* Trigger immediately the first time, subsequently 20ms. */
	k_timepoint_t slow_code = sys_timepoint_calc(K_NO_WAIT);

	while (true) {
		/* handler for therm trip */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.therm_trip_triggered) {
				chip->data.therm_trip_triggered = false;

				if (board_fault_led.port != NULL) {
					gpio_pin_set_dt(&board_fault_led, 1);
				}

				if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
					set_fan_speed(100);
				}
				bh_chip_reset_chip(chip, true);
				bh_chip_cancel_bus_transfer_clear(chip);
			}
		}

		/* handler for PERST */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.trigger_reset) {
				chip->data.trigger_reset = false;
				if (chip->data.workaround_applied) {
					jtag_bootrom_reset_asic(chip);
					jtag_bootrom_soft_reset_arc(chip);
					jtag_bootrom_teardown(chip);
					chip->data.needs_reset = false;
				} else {
					chip->data.needs_reset = true;
				}
				bh_chip_cancel_bus_transfer_clear(chip);
			}
		}

		/* handler for PGOOD */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			handle_pgood_event(chip, board_fault_led);
		}

		/* TODO(drosen): Turn this into a task which will re-arm until static data is sent
		 */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.arc_needs_init_msg) {
				if (bh_chip_set_static_info(chip, &static_info) == 0 &&
				    bh_chip_set_input_power_lim(chip, max_power) == 0 &&
				    bh_chip_run_smbus_tests(chip) == 0) {
					chip->data.arc_needs_init_msg = false;
				}
			}
		}

		if (IS_ENABLED(CONFIG_INA228)) {
			ina228_power_update();
		}

		if (sys_timepoint_expired(slow_code)) {
			if (IS_ENABLED(CONFIG_TT_FAN_CTRL)) {
				uint16_t rpm = get_fan_rpm();

				ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
					bh_chip_set_fan_rpm(chip, rpm);
				}
			}

			ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
				process_cm2dm_message(chip);
			}

			slow_code = sys_timepoint_calc(K_MSEC(20));
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
