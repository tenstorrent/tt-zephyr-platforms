/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/mspi.h>
#include <zephyr/drivers/mspi/mspi_dw.h>
#include <string.h>

#define SPI_RX_TRAIN_ADDR 0x13FFC
#define SPI_RX_TRAIN_DATA 0xa5a55a5a

const struct device *mspi_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi0));
const struct device *flash = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(spi_flash));

static int tt_blackhole_init(void)
{
	/* To avoid false positive */
	uint32_t spi_rx_buf = 0xDEADBEEF;
	int rc, rx_delay = -1;
	int delay_lb, delay_ub;

	if ((!device_is_ready(flash)) || (!device_is_ready(mspi_dev))) {
		return -ENODEV;
	}

	/*
	 * Perform flash training here. We need to train the RX sample delay
	 * to be sure we have valid reads at higher frequencies
	 */

	/* First, find the lower delay setting that works */
	do {
		rx_delay++;
		rc = mspi_timing_config(mspi_dev, NULL, MSPI_DW_RX_TIMING_CFG, (void *)rx_delay);
		if (rc < 0) {
			return rc;
		}
		rc = flash_read(flash, SPI_RX_TRAIN_ADDR, &spi_rx_buf, sizeof(spi_rx_buf));
		if (rc < 0) {
			return rc;
		}
	} while ((spi_rx_buf != SPI_RX_TRAIN_DATA) && (rx_delay < 255));
	delay_lb = rx_delay;
	/* Find the upper bound on the delay setting */
	do {
		rx_delay++;
		rc = mspi_timing_config(mspi_dev, NULL, MSPI_DW_RX_TIMING_CFG, (void *)rx_delay);
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
	return mspi_timing_config(mspi_dev, NULL, MSPI_DW_RX_TIMING_CFG, (void *)rx_delay);
}

SYS_INIT(tt_blackhole_init, POST_KERNEL, CONFIG_BOARD_INIT_PRIORITY);
