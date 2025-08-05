/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

/* This file implements the programming routines for various SPI EEPROMs.
 */

#include <stddef.h>
#include <string.h>

#include "spi_hal.h"

/* Configuration for EEPROM */
struct spi_nor_config {
	uint8_t read_cmd; /* Read command */
	uint8_t pp_cmd;   /* Page program command */
	uint8_t se_cmd;   /* Sector erase command */
	uint8_t ce_cmd;   /* Chip erase command */
};

struct spi_nor_chip {
	uint32_t jedec_id; /* JEDEC ID of the chip */
	const struct spi_nor_config config;
};

#define SPI_NOR_WIP_BIT 0x01 /* Write In Progress bit in status register */

/*
 * The data section doesn't work with PIC code for Cortex-M0+,
 * so we probe the EEPROM at runtime to find the correct configuration,
 * and allocate everything on the stack.
 */
static int eeprom_probe(struct spi_nor_config *cfg)
{
	struct spi_buf buf[2];
	uint32_t jedec_id = 0;
	int ret;
	uint8_t cmd = 0x9F; /* Read JEDEC ID command */

	buf[0].tx_buf = &cmd;
	buf[0].rx_buf = NULL;
	buf[0].len = 1;
	buf[1].tx_buf = NULL;
	buf[1].rx_buf = (uint8_t *)&jedec_id;
	buf[1].len = 3;
	struct spi_nor_chip eeproms[] = {
		/* clang-format off */
		{
			.jedec_id = 0x20BB20, /* JEDEC ID for MT25QU512ABB */
			.config = {
					.read_cmd = 0x13,
					.pp_cmd = 0x12,
					.se_cmd = 0x21,
					.ce_cmd = 0xC7,
				},
		},
		{
			.jedec_id = 0x1A5B2C, /* JEDEC ID for MT35XU02GCBA */
			.config = {
					.read_cmd = 0x13,
					.pp_cmd = 0x12,
					.se_cmd = 0x21,
					/* 0x60 is a common SPI NOR chip erase command.
					 * 0xC4 given in datasheet doesn't work, so use this.
					 * This is undocumented behavior!
					 */
					.ce_cmd = 0x60,
				},
		},
		/* clang-format on */
		{.jedec_id = 0, .config = {0}} /* Terminator */
	};
	struct spi_nor_chip *chip;

	/* Read JEDEC ID, match to eeprom */
	ret = spi_transfer(buf, 2);
	if (ret != 0) {
		return ret;
	}

	for (chip = &eeproms[0]; chip->jedec_id != 0; chip++) {
		if (jedec_id == chip->jedec_id) {
			memcpy(cfg, &chip->config, sizeof(struct spi_nor_config));
			return 0;
		}
	}
	return -1; /* No matching chip found */
}

static void fill_addr(uint8_t *addr_buf, uint32_t addr)
{
	addr_buf[0] = (addr >> 24) & 0xFF; /* MSB */
	addr_buf[1] = (addr >> 16) & 0xFF;
	addr_buf[2] = (addr >> 8) & 0xFF;
	addr_buf[3] = addr & 0xFF; /* LSB */
}

static int wait_spi_ready(void)
{
	int ret;
	/* Waits for the SPI to clear the WIP bit (bit 0 of status register 0) */
	struct spi_buf buf[2];
	uint8_t status_cmd = 0x05; /* Read status register command */
	uint8_t status = SPI_NOR_WIP_BIT;

	buf[0].tx_buf = &status_cmd;
	buf[0].rx_buf = NULL;
	buf[0].len = 1; /* 1 byte command */
	buf[1].tx_buf = NULL;
	buf[1].rx_buf = &status;
	buf[1].len = sizeof(status);
	/* PYOCD will simply timeout if the spi flash never clears the WIP bit */
	while (status & SPI_NOR_WIP_BIT) {
		ret = spi_transfer(buf, 2);
		if (ret != 0) {
			return ret; /* SPI transfer failed */
		}
	}
	return 0; /* SPI ready */
}

static int spi_write_enable(void)
{
	uint8_t cmd = 0x06; /* Write Enable command */
	struct spi_buf buf;

	buf.tx_buf = &cmd;
	buf.rx_buf = NULL;
	buf.len = 1;                  /* 1 byte command */
	return spi_transfer(&buf, 1); /* Send write enable command */
}

int eeprom_init(void)
{
	struct spi_nor_config cfg;

	return eeprom_probe(&cfg);
}

int eeprom_deinit(void)
{
	return 0; /* No deinit required */
}

int eeprom_erase_chip(void)
{
	struct spi_nor_config cfg;
	struct spi_buf buf;
	int ret;

	if (eeprom_probe(&cfg) != 0) {
		return -1; /* EEPROM probe failed */
	}

	ret = spi_write_enable();
	if (ret != 0) {
		return ret; /* Write enable failed */
	}

	buf.tx_buf = &cfg.ce_cmd; /* Chip erase command */
	buf.rx_buf = NULL;
	buf.len = 1;
	ret = spi_transfer(&buf, 1);
	if (ret != 0) {
		return ret; /* SPI transfer failed */
	}
	return wait_spi_ready(); /* Wait for the chip to be ready */
}

int eeprom_erase_sector(uint32_t sector)
{
	struct spi_nor_config cfg;
	struct spi_buf buf;
	uint8_t cmd_buf[5];
	int ret;

	if (eeprom_probe(&cfg) != 0) {
		return -1; /* EEPROM probe failed */
	}

	ret = spi_write_enable();
	if (ret != 0) {
		return ret; /* Write enable failed */
	}

	/* Populate command buffer */
	cmd_buf[0] = cfg.se_cmd;        /* Erase command */
	fill_addr(&cmd_buf[1], sector); /* Address in big-endian format */

	buf.tx_buf = cmd_buf;
	buf.rx_buf = NULL;
	buf.len = 5; /* 1 byte command + 4 bytes address */
	ret = spi_transfer(&buf, 1);
	if (ret != 0) {
		return ret; /* SPI transfer failed */
	}
	return wait_spi_ready(); /* Wait for the sector to be ready */
}

int eeprom_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
	struct spi_nor_config cfg;
	struct spi_buf buf[2];
	uint8_t cmd_buf[5];
	int ret;

	if (len == 0 || data == NULL) {
		return -1; /* Invalid parameters */
	}

	if (len % 0x1000 != 0 || addr % 0x1000 != 0) {
		return -1; /* Address and length must be aligned to 4KB */
	}

	if (data == NULL || len == 0) {
		return -1; /* Invalid parameters */
	}

	if (eeprom_probe(&cfg) != 0) {
		return -1; /* EEPROM probe failed */
	}

	/* Populate command buffer */
	cmd_buf[0] = cfg.pp_cmd; /* Program command */

	for (uint32_t off = 0; off < len; off += 0x100) {
		ret = spi_write_enable();
		if (ret != 0) {
			return ret; /* Write enable failed */
		}

		fill_addr(&cmd_buf[1], addr + off); /* Address in big-endian format */

		buf[0].tx_buf = cmd_buf;
		buf[0].rx_buf = NULL;
		buf[0].len = 5; /* 1 byte command + 4 bytes address */
		buf[1].tx_buf = &data[off];
		buf[1].rx_buf = NULL;
		buf[1].len = 0x100; /* Program 256 bytes */
		ret = spi_transfer(buf, 2);
		if (ret != 0) {
			return ret; /* SPI transfer failed */
		}
		ret = wait_spi_ready(); /* Wait for the EEPROM to be ready */
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

int eeprom_read(uint32_t addr, uint8_t *data, uint32_t len)
{
	struct spi_nor_config cfg;
	struct spi_buf buf[2];
	uint8_t cmd_buf[5];

	if (eeprom_probe(&cfg) != 0) {
		return -1; /* EEPROM probe failed */
	}

	if (data == NULL || len == 0) {
		return -1; /* Invalid parameters */
	}

	/* Populate command buffer */
	cmd_buf[0] = cfg.read_cmd;    /* Read command */
	fill_addr(&cmd_buf[1], addr); /* Address in big-endian format */

	buf[0].tx_buf = cmd_buf;
	buf[0].rx_buf = NULL;
	buf[0].len = 5; /* 1 byte command + 4 bytes address */
	buf[1].tx_buf = NULL;
	buf[1].rx_buf = data;
	buf[1].len = len;
	return spi_transfer(buf, 2);
}
