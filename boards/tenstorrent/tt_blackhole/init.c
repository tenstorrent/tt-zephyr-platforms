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

/* SPI operating modes, defined by blackhole bootrom */
typedef enum {
	SpiStandardMode = 0,
	SpiDualMode = 1,
	SpiQuadMode = 2,
	SpiOctalMode = 3,
} SpiIoMode;

/* Reset unit SPI configuration register, indicates mode of SPI device. */
typedef struct {
	uint32_t boot_spi_mode: 2;
	uint32_t boot_ddr: 1;
	uint32_t boot_dqs: 1;
	uint32_t boot_address_mode: 4;
	uint32_t normal_spi_mode: 2;
	uint32_t normal_ddr: 1;
	uint32_t normal_dqs: 1;
	uint32_t normal_address_mode: 4;
	uint32_t device_addr_bytes: 4;
	uint32_t flash_family: 2;
} RESET_UNIT_SPI_DEVICE_CONFIG_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_SPI_DEVICE_CONFIG_reg_t f;
} RESET_UNIT_SPI_DEVICE_CONFIG_reg_u;

#define RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR 0x800300D4
#define RESET_UNIT_SPI_DEVICE_ID_REG_ADDR 0x800300D8

static int flash_reset_init(void)
{
	RESET_UNIT_SPI_DEVICE_CONFIG_reg_u spi_device_config;
	uint32_t spi_device_id;
	int rc;
	struct mspi_xfer_packet packet = {
		.dir = MSPI_TX,
	};
	struct mspi_xfer xfer = {
		.xfer_mode = MSPI_PIO,
		.packets = &packet,
		.num_packet = 1,
		.timeout = 10, /* 10 MSEC transfer timeout */
		.cmd_length = 1,
	};
	struct mspi_dev_cfg mspi_dev_cfg = {
		.freq = DT_PROP(DT_NODELABEL(spi_flash), mspi_max_frequency),
		.endian = MSPI_XFER_BIG_ENDIAN,
		.cmd_length = 1,
	};
	const struct mspi_dev_id mspi_dev_id = {
		.dev_idx = DT_REG_ADDR(DT_NODELABEL(spi_flash)),
	};

	if (!device_is_ready(mspi_dev)) {
		return -ENODEV;
	}

	/* First, check the operation mode of the SPI controller */
	spi_device_config.val = sys_read32(RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR);
	spi_device_id = sys_read32(RESET_UNIT_SPI_DEVICE_ID_REG_ADDR);

	mspi_dev_cfg.addr_length = spi_device_config.f.normal_address_mode ? 4 : 3;
	xfer.addr_length = 0;

	if ((spi_device_config.f.normal_spi_mode == SpiOctalMode) &&
	    (spi_device_id == 0x2c5b1a10)) {
		/* MX35 flash. Issue RESET ENABLE and RESET MEMORY commands in octal mode */
		mspi_dev_cfg.io_mode = MSPI_IO_MODE_OCTAL;
		rc = mspi_dev_config(mspi_dev, &mspi_dev_id, MSPI_DEVICE_CONFIG_ALL,
				     &mspi_dev_cfg);
		if (rc < 0) {
			return rc;
		}
		/* Send RESET ENABLE */
		packet.cmd = 0x66;
		rc = mspi_transceive(mspi_dev, &mspi_dev_id, &xfer);
		if (rc < 0) {
			return rc;
		}
		/* Delay a minimum of 30 ns before sending next command */
		k_usleep(1);
		/* Send RESET MEMORY */
		packet.cmd = 0x99;
		rc = mspi_transceive(mspi_dev, &mspi_dev_id, &xfer);
		if (rc < 0) {
			return rc;
		}
		/* Delay a minimum of 40 ns to allow reset to complete */
		k_usleep(1);
		/* Write to SPI_DEVICE_CONFIG with new settings */
		spi_device_config.f.normal_spi_mode = SpiStandardMode;
		spi_device_config.f.normal_ddr = 0;
		spi_device_config.f.normal_address_mode = 3;
		spi_device_config.f.device_addr_bytes = 3;
		sys_write32(spi_device_config.val, RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR);
	} else if ((spi_device_config.f.normal_spi_mode == SpiQuadMode) &&
		   (spi_device_id == 0x20bb2010)) {
		if (spi_device_config.f.normal_ddr) {
			/* MT25 flash in DDR mode. */
			mspi_dev_cfg.data_rate = MSPI_DATA_RATE_DUAL;
		}
		/* MT25 flash. Issue RESET ENABLE and RESET MEMORY commands in quad mode */
		mspi_dev_cfg.io_mode = MSPI_IO_MODE_QUAD;
		rc = mspi_dev_config(mspi_dev, &mspi_dev_id, MSPI_DEVICE_CONFIG_ALL,
				     &mspi_dev_cfg);
		if (rc < 0) {
			return rc;
		}
		/* Send RESET ENABLE */
		packet.cmd = 0x66;
		rc = mspi_transceive(mspi_dev, &mspi_dev_id, &xfer);
		if (rc < 0) {
			return rc;
		}
		/* Delay a minimum of 30 ns before sending next command */
		k_usleep(1);
		/* Send RESET MEMORY */
		packet.cmd = 0x99;
		rc = mspi_transceive(mspi_dev, &mspi_dev_id, &xfer);
		if (rc < 0) {
			return rc;
		}
		/* Delay a minimum of 40 ns to allow reset to complete */
		k_usleep(1);
		spi_device_config.f.normal_spi_mode = SpiStandardMode;
		spi_device_config.f.normal_ddr = 0;
		spi_device_config.f.normal_address_mode = 3;
		spi_device_config.f.device_addr_bytes = 3;
		sys_write32(spi_device_config.val, RESET_UNIT_SPI_DEVICE_CONFIG_REG_ADDR);
	}


	/* Release the MSPI controller - it was acquired by the call to
	 * mspi_dev_config()
	 */
	(void)mspi_get_channel_status(mspi_dev, 0);


	return 0;
}

static int flash_training_init(void)
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

SYS_INIT(flash_reset_init, POST_KERNEL, CONFIG_FLASH_RESET_PRIORITY);
SYS_INIT(flash_training_init, POST_KERNEL, CONFIG_FLASH_TRAINING_PRIORITY);
