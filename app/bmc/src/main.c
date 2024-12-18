/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdlib.h>

#include <app_version.h>
#include <tenstorrent/fwupdate.h>
#include <tenstorrent/jtag_bootrom.h>
#include <tenstorrent/tt_smbus.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/smbus.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(main, CONFIG_TT_MAIN_LOG_LEVEL);

static const struct gpio_dt_spec reset_spi = GPIO_DT_SPEC_GET(DT_ALIAS(reset_spi), gpios);

static const struct device *const bh_smbus = DEVICE_DT_GET_OR_NULL(DT_ALIAS(bh_smbus));
static const struct gpio_dt_spec board_fault_led =
  GPIO_DT_SPEC_GET_OR(DT_ALIAS(board_fault_led), gpios, {0});

typedef struct bmStaticInfo {
  // Non-zero for valid data
  // Allows for breaking changes
  uint32_t version;
  uint32_t bl_version;
  uint32_t app_version;
} __attribute__((packed)) bmStaticInfo;

typedef struct cm2bmMessage {
  uint8_t msg_id;
  uint8_t seq_num;
  uint32_t data;
} __attribute__((packed)) cm2bmMessage;

typedef struct cm2bmAck {
  uint8_t msg_id;
  uint8_t seq_num;
} __attribute__((packed)) cm2bmAck;

union cm2bmAckWire {
  cm2bmAck f;
  uint16_t val;
};

static inline int tt_bist(void)
{
  /* TODO: this should probably be a library - just return success for now */
  return 0;
}

int update_fw()
{
  /* To get here we are already running known good fw */
  int ret;

  ret = gpio_pin_configure_dt(&reset_spi, GPIO_OUTPUT_ACTIVE);
  if (ret < 0) {
    LOG_ERR("%s() failed (could not configure the spi_reset pin): %d", "gpio_pin_configure_dt",
            ret);
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

int main(void)
{
  int ret;
  int bist_rc;

  bist_rc = 0;
  if (IS_ENABLED(CONFIG_TT_BIST)) {
    bist_rc = tt_bist();
    if (bist_rc < 0) {
      LOG_ERR("%s() failed: %d", "tt_bist", bist_rc);
    } else {
      LOG_DBG("Built-in self-test succeeded");
    }
  }

  if (IS_ENABLED(CONFIG_TT_FWUPDATE)) {
    if (!tt_fwupdate_is_confirmed()) {
      if (bist_rc < 0) {
        LOG_ERR("Firmware update was unsuccessful and will be rolled-back after bmfw reboot.");
        /*
         * This probably represents a firmware regression that managed to go undetected through
         * developer-level testing, on-diff testing, CI, and soak-testing.
         */
        if (IS_ENABLED(CONFIG_REBOOT)) {
          sys_reboot(SYS_REBOOT_COLD);
        }
        return EXIT_FAILURE;
      }

      /* BIST successful, so confirm fwupdate */
      ret = tt_fwupdate_confirm();
      if (ret < 0) {
        LOG_ERR("%s() failed: %d", "tt_fwupdate_confirm", ret);
        /* This represents a serious problem - could not write to internal SoC flash, etc */
        return EXIT_FAILURE;
      }
    }
  }

  /* Should only happen if EXIT_FAILURE is returned; otherwise will reboot */
  ret = update_fw();
  if (ret != 0) {
    return ret;
  }

  /* IMPORTANT - turn off the SPI Mux select line so that CMFW can read SPI flash */
  const struct device *const spi_mux_gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpiob));
  gpio_pin_t spi_mux_gpio_pin = 5; /* DT_GPIO_HOG_PIN_BY_IDX() does not seem to work */
  ret = gpio_pin_set(spi_mux_gpio_dev, spi_mux_gpio_pin, 0);
  if (ret < 0) {
    LOG_DBG("%s() failed: %d", "gpio_pin_set", ret);
  }

  if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
    gpio_pin_configure_dt(&board_fault_led, GPIO_OUTPUT_ACTIVE);
  }

  if (IS_ENABLED(CONFIG_JTAG_LOAD_BOOTROM)) {
    ret = jtag_bootrom_init();
    if (ret != 0) {
      LOG_ERR("%s() failed: %d", "jtag_bootrom_init", ret);
      return ret;
    }

    ret = jtag_bootrom_reset(false);
    if (ret != 0) {
      LOG_ERR("%s() failed: %d", "jtag_bootrom_reset", ret);
      return ret;
    }

    LOG_DBG("Bootrom workaround successfully applied");
  }

  LOG_DBG("This is the " CONFIG_TT_MAIN_APP_IMAGE_VARIANT " image\n");
  LOG_DBG("BMFW VERSION " APP_VERSION_STRING "\n");

  // No mechanism for getting bl version... yet
  bmStaticInfo static_info =
    (bmStaticInfo){.version = 1, .bl_version = 0, .app_version = APPVERSION};

  if (bh_smbus != NULL) {
    bool send_version_info = true;

    // Set cancel flag for i2c commands
    int *bus_abort = jtag_bootrom_disable_bus();
    tt_smbus_stm32_set_abort_ptr(bh_smbus, bus_abort);

    if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
      gpio_pin_set_dt(&board_fault_led, 0);
    }

    while (1) {
      k_sleep(K_MSEC(20));

      if (IS_ENABLED(CONFIG_TT_ASSEMBLY_TEST) && board_fault_led.port != NULL) {
        // Blink the light every half second or so
        k_sleep(K_MSEC(500 - 20));
        gpio_pin_toggle_dt(&board_fault_led);
      }

      if (send_version_info || was_arc_reset()) {
        if (was_arc_reset()) {
          // Allow retry on failure
          send_version_info = true;
          handled_arc_reset();
        }
        struct k_spinlock reset_lock = jtag_bootrom_reset_lock();
        K_SPINLOCK(&reset_lock) {
          int ret =
            smbus_block_write(bh_smbus, 0xA, 0x20, sizeof(bmStaticInfo), (uint8_t *)&static_info);
          if (ret == 0) {
            send_version_info = false;
          }
        }
        k_yield();
      }

      cm2bmMessage message = {0};
      uint8_t count;

      int ret;
      {
        struct k_spinlock reset_lock = jtag_bootrom_reset_lock();
        K_SPINLOCK(&reset_lock) {
          ret = smbus_block_read(bh_smbus, 0xA, 0x10, &count, (uint8_t *)&message);
        }
        k_yield();
      }

      if (ret == 0 && message.msg_id != 0) {
        cm2bmAck ack = {0};
        ack.msg_id = message.msg_id;
        ack.seq_num = message.seq_num;
        union cm2bmAckWire wire_ack;
        wire_ack.f = ack;
        smbus_word_data_write(bh_smbus, 0xA, 0x11, wire_ack.val);

        switch (message.msg_id) {
        case 0x1:
          switch (message.data) {
          case 0x0:
            jtag_bootrom_reset(true);
            send_version_info = true;
            // Wait 800ms before allowing another reset + 200ms = 1s between resets
            k_sleep(K_MSEC(800));
            break;
          case 0x3:
            // Trigger reboot; will reset asic and reload bmfw
            if (IS_ENABLED(CONFIG_REBOOT)) {
              sys_reboot(SYS_REBOOT_COLD);
            }
            break;
          }
          break;
        }
      }
    }
  }

  return EXIT_SUCCESS;
}
