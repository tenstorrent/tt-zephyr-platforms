/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdlib.h>

#include <app_version.h>
#include <tenstorrent/bist.h>
#include <tenstorrent/fwupdate.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/drivers/jtag.h>
#include <zephyr/drivers/mfd/max6639.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <tenstorrent/bh_chip.h>
#include <tenstorrent/bh_arc.h>
#include <tenstorrent/event.h>
#include <tenstorrent/jtag_bootrom.h>
#include <tenstorrent/tt_smbus_regs.h>

#define RESET_UNIT_ARC_PC_CORE_0 0x80030C00

LOG_MODULE_REGISTER(main, CONFIG_TT_APP_LOG_LEVEL);

BUILD_ASSERT(FIXED_PARTITION_EXISTS(bmfw), "bmfw fixed-partition does not exist");

struct bh_chip BH_CHIPS[BH_CHIP_COUNT] = {DT_FOREACH_PROP_ELEM(DT_PATH(chips), chips, INIT_CHIP)};

#if BH_CHIP_PRIMARY_INDEX >= BH_CHIP_COUNT
#error "Primary chip out of range"
#endif

static const struct gpio_dt_spec board_fault_led =
	GPIO_DT_SPEC_GET_OR(DT_PATH(board_fault_led), gpios, {0});
static const struct device *const ina228 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(ina228));
static const struct device *const max6639_pwm_dev =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(max6639_pwm));
static const struct device *const max6639_sensor_dev =
	DEVICE_DT_GET_OR_NULL(DT_NODELABEL(max6639_sensor));

#if BH_CHIP_COUNT == 2
static uint8_t auto_fan_speed[BH_CHIP_COUNT] = {35, 35}; /* per–chip */
#else
static uint8_t auto_fan_speed[BH_CHIP_COUNT] = {35}; /* per–chip */
#endif
static uint8_t forced_fan_speed; /* 0-100 % */

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

static bool process_reset_req(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	switch (msg_data) {
	case 0x0:
		chip->data.last_cm2dm_seq_num_valid = false;
		chip->data.cm2dm_suspicion = 0;
		bh_chip_reset_chip(chip, true);
		break;

	case 0x3:
		/* Trigger reboot; will reset asic and reload dmfw */
		if (IS_ENABLED(CONFIG_REBOOT)) {
			sys_reboot(SYS_REBOOT_COLD);
		}
		break;
	}

	return true;
}

static bool process_ping(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	/* Respond to ping request from CMFW */
	bharc_smbus_word_data_write(&chip->config.arc, CMFW_SMBUS_PING, 0xA5A5);
	return false;
}

static bool process_fan_speed_update(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
		uint8_t fan_speed_percentage = (uint8_t)msg_data & 0xFF;
		uint8_t fan_speed = (uint8_t)DIV_ROUND_UP(fan_speed_percentage * UINT8_MAX, 100);
		int chip_index = msg_data >> 31;

		pwm_set_cycles(max6639_pwm_dev, 0, UINT8_MAX, fan_speed, 0);
		auto_fan_speed[chip_index] = fan_speed;
	}
	return false;
}

static bool process_forced_fan_speed_update(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
		uint8_t fan_speed_percentage = (uint8_t)msg_data & 0xFF;
		uint8_t fan_speed = (uint8_t)DIV_ROUND_UP(fan_speed_percentage * UINT8_MAX, 100);

		forced_fan_speed = fan_speed;

		/* Broadcast forced speed to all CMFWs for telemetry */
		for (int i = 0; i < BH_CHIP_COUNT; i++) {
			bharc_smbus_word_data_write(&BH_CHIPS[i].config.arc, CMFW_SMBUS_FAN_SPEED,
						    fan_speed_percentage);
		}
	}
	return false;
}

static bool process_id_ready(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	chip->data.arc_needs_init_msg = true;
	return false;
}

static bool process_auto_reset_timeout_update(struct bh_chip *chip, uint8_t msg_id,
					      uint32_t msg_data)
{
	/* Set auto reset timeout */
	chip->data.auto_reset_timeout = msg_data;
	if (chip->data.auto_reset_timeout != 0) {
		/* Start auto-reset timer */
		k_timer_start(&chip->auto_reset_timer, K_MSEC(chip->data.auto_reset_timeout),
			      K_NO_WAIT);
	} else {
		/* Stop auto-reset timer */
		k_timer_stop(&chip->auto_reset_timer);
	}
	return false;
}

static bool process_heartbeat_update(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data)
{
	/* Update telemetry heartbeat */
	if (chip->data.telemetry_heartbeat != msg_data) {
		/* Telemetry heartbeat is moving */
		chip->data.telemetry_heartbeat = msg_data;
		if (chip->data.auto_reset_timeout != 0) {
			/* Restart auto reset timer */
			k_timer_start(&chip->auto_reset_timer,
				      K_MSEC(chip->data.auto_reset_timeout), K_NO_WAIT);
		}
	}
	return false;
}

void process_cm2dm_message(struct bh_chip *chip)
{
	typedef bool (*msg_processor_t)(struct bh_chip *chip, uint8_t msg_id, uint32_t msg_data);

	static const msg_processor_t msg_processors[] = {
		[kCm2DmMsgIdResetReq] = process_reset_req,
		[kCm2DmMsgIdPing] = process_ping,
		[kCm2DmMsgIdFanSpeedUpdate] = process_fan_speed_update,
		[kCm2DmMsgIdForcedFanSpeedUpdate] = process_forced_fan_speed_update,
		[kCm2DmMsgIdReady] = process_id_ready,
		[kCm2DmMsgIdAutoResetTimeoutUpdate] = process_auto_reset_timeout_update,
		[kCm2DmMsgTelemHeartbeatUpdate] = process_heartbeat_update,
	};

	for (unsigned int i = 0; i < kCm2DmMsgCount; i++) {
		cm2dmMessageRet msg = bh_chip_get_cm2dm_message(chip);

		if (msg.ret != 0) {
			/* error already logged by bh_chip_get_cm2dm_message */
			chip->data.cm2dm_suspicion++;
			break;
		}

		if (msg.msg.msg_id == kCm2DmMsgIdNull) {
			/* no messages pending, note that seq_num is not valid */
			break;
		}

		if (chip->data.last_cm2dm_seq_num_valid &&
		    chip->data.last_cm2dm_seq_num == msg.msg.seq_num) {
			/* repeat sequence number, indicates ack failure, try again I guess */
			LOG_ERR("Received duplicate CM2DM message.");
			chip->data.cm2dm_suspicion++;
			continue;
		}

		chip->data.last_cm2dm_seq_num_valid = true;
		chip->data.last_cm2dm_seq_num = msg.msg.seq_num;

		if (msg.msg.msg_id < ARRAY_SIZE(msg_processors) && msg_processors[msg.msg.msg_id]) {
			chip->data.cm2dm_suspicion = 0;
			if (msg_processors[msg.msg.msg_id](chip, msg.msg.msg_id, msg.msg.data)) {
				break;
			}
		} else {
			LOG_ERR("Received unknown CM2DM message ID: %d", msg.msg.msg_id);
			chip->data.cm2dm_suspicion++;
		}

		/* kCm2DmMsgNull is not a valid message so there can never be more than
		 * kCm2DmMsgCount - 2 pending messages. It's possible that more messages are posted
		 * while this loop runs, so this is not solid proof of failure. But it is
		 * suspicious.
		 */
		if (i > kCm2DmMsgCount - 2) {
			chip->data.cm2dm_suspicion++;
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
	uint8_t data[255]; /* Max size of SMBUS block read */
	uint32_t app_version;

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
	if (count != 7) {
		LOG_ERR("SMBUS telemetry read returned unexpected count: %d", count);
		return -EIO;
	}
	if (data[0] != 0U) {
		LOG_ERR("SMBUS telemetry read returned invalid telem idx");
		return -EIO;
	}
	(void)memcpy(&app_version, &data[3], sizeof(app_version));

	if (app_version != APPVERSION) {
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

	bist_rc = 0;
	if (IS_ENABLED(CONFIG_TT_BIST)) {
		bist_rc = tt_bist();
		if (bist_rc < 0) {
			LOG_ERR("%s() failed: %d", "tt_bist", bist_rc);
		} else {
			LOG_DBG("Built-in self-test succeeded");
		}
	}

	if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
		uint8_t fan_speed =
			(uint8_t)DIV_ROUND_UP(35 * UINT8_MAX, 100); /* Start fan speed at 35% */

		pwm_set_cycles(max6639_pwm_dev, 0, UINT8_MAX, fan_speed, 0);
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

	ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
		const struct device *smbus = chip->config.arc.smbus.bus;

		smbus_configure(smbus, SMBUS_MODE_CONTROLLER | SMBUS_MODE_PEC);
	}

	printk("DMFW VERSION " APP_VERSION_STRING "\n");

	if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
		gpio_pin_set_dt(&board_fault_led, 1);
	}

	/* No mechanism for getting bl version... yet */
	dmStaticInfo static_info =
		(dmStaticInfo){.version = 1, .bl_version = 0, .app_version = APPVERSION};

	uint16_t max_power = detect_max_power();

	while (true) {
		tt_event_wait(TT_EVENT_WAKE, K_MSEC(20));

		/* handler for therm trip */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.therm_trip_triggered) {
				chip->data.therm_trip_triggered = false;
				chip->data.last_cm2dm_seq_num_valid = false;
				chip->data.cm2dm_suspicion = 0;

				if (board_fault_led.port != NULL) {
					gpio_pin_set_dt(&board_fault_led, 1);
				}

				if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
					pwm_set_cycles(max6639_pwm_dev, 0, UINT32_MAX, UINT32_MAX,
						       0);
				}

				/* Prioritize the system rebooting over the therm trip handler */
				if (!atomic_get(&chip->data.trigger_reset)) {
					/* Technically trigger_reset could have been set here but I
					 * think I'm happy to eat the non-enum in that case
					 */
					chip->data.performing_reset = true;
					/* Set the bus cancel following the logic of
					 * (reset_triggered && !performing_reset)
					 */
					bh_chip_cancel_bus_transfer_clear(chip);

					chip->data.therm_trip_count++;
					bh_chip_reset_chip(chip, true);

					/* Set the bus cancel following the logic of
					 * (reset_triggered && !performing_reset)
					 */
					if (atomic_get(&chip->data.trigger_reset)) {
						bh_chip_cancel_bus_transfer_set(chip);
					}
					chip->data.performing_reset = false;
				}
			}
		}

		/* Handler for watchdog trigger */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (chip->data.arc_wdog_triggered) {
				chip->data.arc_wdog_triggered = false;
				chip->data.last_cm2dm_seq_num_valid = false;
				chip->data.cm2dm_suspicion = 0;

				/* Read PC from ARC and record it */
				jtag_setup(chip->config.jtag);
				jtag_reset(chip->config.jtag);
				jtag_axi_read32(chip->config.jtag, RESET_UNIT_ARC_PC_CORE_0,
						&chip->data.arc_hang_pc);
				jtag_teardown(chip->config.jtag);
				/* Clear watchdog state */
				chip->data.auto_reset_timeout = 0;

				if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
					pwm_set_cycles(max6639_pwm_dev, 0, UINT32_MAX, UINT32_MAX,
						       0);
				}

				chip->data.performing_reset = true;
				bh_chip_reset_chip(chip, true);
				/* Clear bus transfer cancel flag */
				bh_chip_cancel_bus_transfer_clear(chip);

				chip->data.performing_reset = false;
			}
		}

		/* handler for PERST */
		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			if (atomic_set(&chip->data.trigger_reset, false)) {
				chip->data.performing_reset = true;
				chip->data.last_cm2dm_seq_num_valid = false;
				chip->data.cm2dm_suspicion = 0;

				/*
				 * Set the bus cancel following the logic of (reset_triggered &&
				 * !performing_reset)
				 */
				bh_chip_cancel_bus_transfer_clear(chip);

				jtag_bootrom_reset_asic(chip);
				jtag_bootrom_soft_reset_arc(chip);
				jtag_bootrom_teardown(chip);

				/*
				 * Set the bus cancel following the logic of (reset_triggered &&
				 * !performing_reset)
				 */
				if (atomic_get(&chip->data.trigger_reset)) {
					bh_chip_cancel_bus_transfer_set(chip);
				}
				chip->data.therm_trip_count = 0;
				chip->data.arc_hang_pc = 0;
				chip->data.performing_reset = false;
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
				    bh_chip_set_therm_trip_count(
					    chip, chip->data.therm_trip_count) == 0 &&
				    bh_chip_run_smbus_tests(chip) == 0) {
					chip->data.arc_needs_init_msg = false;
				}
			}
		}

		if (IS_ENABLED(CONFIG_INA228)) {
			ina228_power_update();
		}

		if (DT_NODE_HAS_STATUS(DT_ALIAS(fan0), okay)) {
			uint16_t rpm;
			struct sensor_value data;

			sensor_sample_fetch_chan(max6639_sensor_dev, MAX6639_CHAN_1_RPM);
			sensor_channel_get(max6639_sensor_dev, MAX6639_CHAN_1_RPM, &data);

			rpm = (uint16_t)data.val1;

			ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
				bh_chip_set_fan_rpm(chip, rpm);
			}

			if (forced_fan_speed != 0) {
				pwm_set_cycles(max6639_pwm_dev, 0, UINT8_MAX, forced_fan_speed, 0);
			} else {
				uint8_t max_fan_speed = auto_fan_speed[0];

				if (BH_CHIP_COUNT == 2) {
					max_fan_speed = MAX(auto_fan_speed[0], auto_fan_speed[1]);
				}
				pwm_set_cycles(max6639_pwm_dev, 0, UINT8_MAX, max_fan_speed, 0);
			}
		}

		ARRAY_FOR_EACH_PTR(BH_CHIPS, chip) {
			process_cm2dm_message(chip);
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
