/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/spi_dw_flash.h>
#include <string.h>

#define SPI_RX_TRAIN_ADDR 0x13FFC
#define SPI_RX_TRAIN_DATA 0xa5a55a5a

const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));

/*
 * Handles reclocking event for SPI controller. We must program the new clock
 * frequency to the SPI controller, and recalibrate the RX sample delay
 */
int spi_controller_reclock(uint32_t freq)
{
	/* To avoid false positive */
	uint32_t spi_rx_buf = 0xDEADBEEF;
	int rc, rx_delay = -1;
	int delay_lb, delay_ub;

	if (!device_is_ready(flash)) {
		return -ENODEV;
	}

	/* Program the new frequency */
	rc = flash_ex_op(flash, FLASH_EX_OP_SPI_DW_CLK_FREQ, freq, NULL);
	if (rc < 0) {
		return rc;
	}

	/*
	 * Perform flash training here. We need to train the RX sample delay
	 * to be sure we have valid reads at higher frequencies
	 */

	/* First, find the lower delay setting that works */
	do {
		rx_delay++;
		rc = flash_ex_op(flash, FLASH_EX_OP_SPI_DW_RX_DLY, rx_delay, NULL);
		if (rc < 0) {
			return rc;
		}
		rc = flash_read(flash, SPI_RX_TRAIN_ADDR, &spi_rx_buf, sizeof(spi_rx_buf));
		if (rc < 0) {
			return rc;
		}
	} while ((spi_rx_buf != SPI_RX_TRAIN_DATA) && (rx_delay < 255));
	if ((rx_delay == 255) && (spi_rx_buf != SPI_RX_TRAIN_DATA)) {
		/* We could not find a good training point */
		return -EIO;
	}
	delay_lb = rx_delay;
	/* Find the upper bound on the delay setting */
	do {
		rx_delay++;
		rc = flash_ex_op(flash, FLASH_EX_OP_SPI_DW_RX_DLY, rx_delay, NULL);
		if (rc < 0) {
			return rc;
		}
		rc = flash_read(flash, SPI_RX_TRAIN_ADDR, &spi_rx_buf, sizeof(spi_rx_buf));
		if (rc < 0) {
			return rc;
		}
	} while ((spi_rx_buf == SPI_RX_TRAIN_DATA) && (rx_delay < 255));
	delay_ub = rx_delay - 1;

	/* Find midpoint of both delay settings */
	rx_delay = (delay_ub - delay_lb) / 2 + delay_lb;
	return flash_ex_op(flash, FLASH_EX_OP_SPI_DW_RX_DLY, rx_delay, NULL);
}

static int tt_blackhole_init(void)
{
	return spi_controller_reclock(DT_PROP(DT_NODELABEL(sysclk), clock_frequency));
}

SYS_INIT(tt_blackhole_init, POST_KERNEL, CONFIG_BOARD_INIT_PRIORITY);
