/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Based on spi_dw.c driver, which is:
 * Copyright (c) 2015 Intel Corporation.
 * Copyright (c) 2023 Synopsys, Inc. All rights reserved.
 * Copyright (c) 2023 Meta Platforms
 */

#define DT_DRV_COMPAT snps_designware_spi

#define LOG_LEVEL CONFIG_FLASH_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_dw_flash);

#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/spi_dw_flash.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/byteorder.h>
#ifdef CONFIG_PINCTRL
#include <zephyr/drivers/pinctrl.h>
#endif

#include "spi_dw_flash.h"
#include "jesd216.h"
#include "spi_nor.h"

/*
 * Flash devices known to this driver, where we apply higher
 * performance settings using vendor specific commands
 */
static const struct spi_dw_flash_entry flash_devs[] = {
	/* JEDEC ID, mode, addr_len */
	{0x2C5B1A, {SPI_DW_ACCESS_1_8_8, 4, /* MT35XU02GCBA */
	/* read_cmd, read_dummy, ce_cmd, se_cmd, be_cmd, pp_cmd, ssize, bsize */
	   0xCC,     16,         0xC4,   0x21,   0xDC,   0x8E,   KB(4), KB(128)}},
	/* JEDEC ID, mode, addr_len */
	{0x20BB20, {SPI_DW_ACCESS_1_4_4, 4, /* MT25QU512ABB */
	/* read_cmd, read_dummy, ce_cmd, se_cmd, be_cmd, pp_cmd, ssize, bsize */
	   0xEC,     10,         0xC7,   0x21,   0xDC,   0x34,   KB(4), KB(64)}}
};

/*
 * Default settings that are lower performance but should work with most
 * flash devices
 */
static const struct spi_dw_flash fallback_flash = {
	.mode = SPI_DW_ACCESS_1_1_1,
	.addr_len = 3,
	.read_cmd = SPI_NOR_CMD_READ,
	.ce_cmd = SPI_NOR_CMD_CE,
	.se_cmd = SPI_NOR_CMD_SE,
	.be_cmd = SPI_NOR_CMD_BE,
	.pp_cmd = SPI_NOR_CMD_PP,
	.ssize = SPI_NOR_SECTOR_SIZE,
	.bsize = SPI_NOR_BLOCK_SIZE,
};

static const struct flash_parameters flash_nor_parameters = {
	.write_block_size = 1,
	.erase_value = 0xff,
};

static void spi_dw_flash_isr(const struct device *dev)
{
	struct spi_dw_flash_data *dev_data = dev->data;
	uint32_t data;
	uint32_t risr = read_risr(dev);

	if (risr & (DW_SPI_ISR_RXOIS | DW_SPI_ISR_RXUIS)) {
		/* RX overrun or underflow */
		LOG_ERR("RX overrun or underflow");
		dev_data->err_state = risr;
		goto out;
	}

	if (risr & DW_SPI_ISR_RXFIS) {
		/* RX FIFO threshold interrupt */
		while (read_rxflr(dev)) {
			/* Pop from RX FIFO */
			data = read_dr(dev);

			/* Copy to RX buffer */
			*dev_data->rx_pos = data & 0xFF;
			dev_data->rx_pos++;
			dev_data->rx_len--;

			if (dev_data->rx_len == 0) {
				/* Wait for FIFO to drain */
				while (test_bit_sr_busy(dev)) {
				}
				/* Disable RX threshold interrupt */
				write_rxftlr(dev, 0);
				write_imr(dev, DW_SPI_IMR_MASK);
				clear_bit_ssienr(dev);
				write_ser(dev, 0);
				k_sem_give(&dev_data->isr_sem);
				goto out;
			}
		}

		if (read_rxftlr(dev) >= (dev_data->rx_len - 1)) {
			write_rxftlr(dev, dev_data->rx_len - 1);
		}
	} else if (risr & DW_SPI_ISR_TXEIS) {
		/* Wait for FIFO to drain */
		while (test_bit_sr_busy(dev)) {
		}
		/* TX FIFO has drained, post to semaphore and disable interrupt */
		write_imr(dev, DW_SPI_IMR_MASK);
		clear_bit_ssienr(dev);
		write_ser(dev, 0);
		k_sem_give(&dev_data->isr_sem);
	}
out:
	/* Clear interrupts */
	clear_interrupts(dev);
}

/* Helpers to take and release SPI controller lock */
static int spi_dw_lock(const struct device *dev)
{
	struct spi_dw_flash_data *data = dev->data;

	return k_sem_take(&data->bus_lock, K_FOREVER);
}

static void spi_dw_unlock(const struct device *dev)
{
	struct spi_dw_flash_data *data = dev->data;

	k_sem_give(&data->bus_lock);
}

/* Helper to perform SPI TX transactions */
static int spi_dw_tx(const struct device *dev,
		     uint8_t opcode, uint32_t addr, uint32_t addr_len,
		     const uint8_t *tx_buf, uint32_t tx_len, uint8_t cs_idx,
		     uint32_t clock_freq, enum spi_dw_access_mode mode)
{
	const struct spi_dw_flash_config *cfg = dev->config;
	struct spi_dw_flash_data *data = dev->data;
	uint32_t ctrlr0 = read_ctrlr0(dev);
	int ret;

	/*
	 * We only support writing up to the TX FIFO depth in one transaction.
	 * This is because the CS line will be de-asserted when the TX FIFO
	 * is empty, so we split all flash write operations into TX FIFO sized
	 * blocks
	 */
	if ((sizeof(opcode) + addr_len + tx_len) > cfg->fifo_depth) {
		LOG_ERR("TX buffer too large");
		return -EINVAL;
	}

	/* Program baudr */
	write_baudr(dev, SPI_DW_CLK_DIVIDER(cfg->clock_frequency, clock_freq));

	/* Program SPI for TX mode */
	ctrlr0 &= ~DW_SPI_CTRLR0_TMOD_RESET;
	ctrlr0 |= DW_SPI_CTRLR0_TMOD_TX;
	write_ctrlr0(dev, ctrlr0);

	/* Assert TXE interrupt when TX FIFO drains */
	write_txftlr(dev, 0);

	/* Enable SSI and program TX FIFO */
	set_bit_ssienr(dev);
	write_dr(dev, opcode);
	if (mode == SPI_DW_ACCESS_1_1_1) {
		for (int i = addr_len; i > 0; i--) {
			write_dr(dev, (addr >> ((i - 1) * 8)) & 0xFF);
		}
	} else {
		if (addr_len > 0) {
			/* For extended SPI mode, we write address as 32 bit value */
			write_dr(dev, addr);
		}
	}

	/* Write tx buffer */
	for (int i = 0; i < tx_len; i++) {
		write_dr(dev, tx_buf[i]);
	}

	/* Now that the TX FIFO has data, enable the TX FIFO empty interrupt */
	write_imr(dev, DW_SPI_IMR_TXEIM);

	LOG_DBG("Starting TX transaction");
	write_ser(dev, BIT(cs_idx));

	/* Wait for TX FIFO empty interrupt */
	ret = k_sem_take(&data->isr_sem, K_MSEC(CONFIG_FLASH_SPI_DW_TIMEOUT));
	if (ret < 0) {
		LOG_ERR("Timeout waiting for TX transaction");
		return ret;
	}
	return data->err_state;
}

/* Helper to perform SPI eeprom transaction */
static int spi_dw_eeprom_transceive(const struct device *dev,
				    uint8_t opcode, uint32_t addr,
				    uint32_t addr_len, uint8_t *rx_buf,
				    uint32_t rx_len, uint8_t cs_idx,
				    uint32_t clock_freq,
				    enum spi_dw_access_mode mode)
{
	const struct spi_dw_flash_config *cfg = dev->config;
	struct spi_dw_flash_data *data = dev->data;
	uint32_t ctrlr0 = read_ctrlr0(dev);
	uint32_t rxftlr = (cfg->fifo_depth * 5) / 8;
	int ret;

	if (rx_len > UINT16_MAX + 1) {
		LOG_ERR("RX buffer too large");
		return -EINVAL;
	}

	if (rx_len == 0) {
		/* This function isn't needed- just call the TX function */
		return spi_dw_tx(dev, opcode, addr, addr_len, NULL, 0, cs_idx,
				 clock_freq, mode);
	}

	/* Program baudr */
	write_baudr(dev, SPI_DW_CLK_DIVIDER(cfg->clock_frequency, clock_freq));

	/* Program NDF */
	write_ctrlr1(dev, rx_len - 1);
	/* Program SPI for eeprom mode */
	ctrlr0 &= ~DW_SPI_CTRLR0_TMOD_RESET;
	ctrlr0 |= DW_SPI_CTRLR0_TMOD_EEPROM;
	write_ctrlr0(dev, ctrlr0);

	if (addr_len + 1 > cfg->fifo_depth) {
		LOG_ERR("Address length too large");
		return -EINVAL;
	}

	/* Setup RX context */
	data->rx_pos = rx_buf;
	data->rx_len = rx_len;
	data->err_state = 0;

	/* Program RX FIFO threshold */
	if (rxftlr > (rx_len - 1)) {
		rxftlr = rx_len - 1;
	}
	write_rxftlr(dev, rxftlr);

	/* Enable RX threshold interrupt */
	write_imr(dev, DW_SPI_IMR_RXFIM);

	/* Enable SSI and program TX FIFO */
	set_bit_ssienr(dev);
	write_dr(dev, opcode);
	if (mode == SPI_DW_ACCESS_1_1_1) {
		for (int i = addr_len; i > 0; i--) {
			write_dr(dev, (addr >> ((i - 1) * 8)) & 0xFF);
		}
	} else {
		if (addr_len > 0) {
			/* For extended SPI mode, we write address as 32 bit value */
			write_dr(dev, addr);
		}
	}

	LOG_DBG("Starting eeprom transaction");

	write_ser(dev, BIT(cs_idx));
	ret = k_sem_take(&data->isr_sem, K_MSEC(CONFIG_FLASH_SPI_DW_TIMEOUT));
	if (ret < 0) {
		LOG_ERR("Timeout waiting for EEPROM transaction");
		return ret;
	}
	return data->err_state;
}

/* Helper to program SPI DW for extended SPI modes */
static int spi_dw_prog_extended(const struct device *dev,
				enum spi_dw_access_mode mode,
				uint8_t addr_len, uint8_t dummy)
{
	uint32_t ctrlr0 = read_ctrlr0(dev);
	uint32_t spi_ctrlr0 = 0U;

	ctrlr0 &= ~DW_SPI_CTRLR0_FRF_RESET;
	spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_WAIT_CYCLES(dummy);
	spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_INST_L(2); /* 8 bit instruction */
	/* Set addr len- 0x8 means 32 bit address, 0x6 means 24 bit address */
	spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_ADDR_L(addr_len * 2);

	switch (mode) {
	case SPI_DW_ACCESS_1_1_1:
		ctrlr0 |= DW_SPI_CTRLR0_FRF_STD;
		break;
	case SPI_DW_ACCESS_1_1_2:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(0);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_DUAL;
		break;
	case SPI_DW_ACCESS_1_2_2:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(1);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_DUAL;
		break;
	case SPI_DW_ACCESS_2_2_2:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(2);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_DUAL;
		break;
	case SPI_DW_ACCESS_1_1_4:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(0);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_QUAD;
		break;
	case SPI_DW_ACCESS_1_4_4:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(1);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_QUAD;
		break;
	case SPI_DW_ACCESS_4_4_4:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(2);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_QUAD;
		break;
	case SPI_DW_ACCESS_1_1_8:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(0);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_OCTAL;
		break;
	case SPI_DW_ACCESS_1_8_8:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(1);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_OCTAL;
		break;
	case SPI_DW_ACCESS_8_8_8:
		spi_ctrlr0 |= DW_SPI_SPI_CTRLR0_TRANS_TYPE(2);
		ctrlr0 |= DW_SPI_CTRLR0_FRF_OCTAL;
		break;
	default:
		LOG_ERR("Unsupported SPI DW access mode");
		return -EINVAL;
	}
	LOG_DBG("ctrlr0: 0x%x, spi_ctrlr0: 0x%x", ctrlr0, spi_ctrlr0);
	write_ctrlr0(dev, ctrlr0);
	if (mode != SPI_DW_ACCESS_1_1_1) {
		write_spi_ctrlr0(dev, spi_ctrlr0);
	}
	return 0;
}

static int spi_dw_flash_dev_read(const struct device *dev,
				 off_t offset,
				 void *data,
				 size_t len)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	struct spi_dw_flash_dev_data *dev_data = dev->data;
	int rc;

	rc = spi_dw_lock(cfg->parent_dev);
	if (rc < 0) {
		return rc;
	}

	rc = spi_dw_prog_extended(cfg->parent_dev,
				  dev_data->flash_cfg->mode,
				  dev_data->flash_cfg->addr_len,
				  dev_data->flash_cfg->read_dummy);
	if (rc < 0) {
		goto out;
	}

	/* Read at full frequency */
	rc = spi_dw_eeprom_transceive(cfg->parent_dev,
				      dev_data->flash_cfg->read_cmd,
				      offset, dev_data->flash_cfg->addr_len,
				      data, len, cfg->cs_idx,
				      cfg->target_freq,
				      dev_data->flash_cfg->mode);

out:
	spi_dw_unlock(cfg->parent_dev);
	return rc;
}

/* Helper to wait for flash to clear BUSY bit in status register 0 */
static int spi_dw_flash_wait_idle(const struct device *dev)
{
	uint64_t ts = k_uptime_get();
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	int rc;
	uint8_t sr;

	rc = spi_dw_prog_extended(cfg->parent_dev,
				  SPI_DW_ACCESS_1_1_1,
				  0, 0);
	if (rc < 0) {
		return rc;
	}
	do {
		/* Poll status register until busy bit is clear */
		rc = spi_dw_eeprom_transceive(cfg->parent_dev,
					      SPI_NOR_CMD_RDSR, 0, 0,
					      &sr, 1, cfg->cs_idx,
					      cfg->target_freq,
					      SPI_DW_ACCESS_1_1_1);
		if (rc < 0) {
			return rc;
		}
		if ((k_uptime_get() - ts) > CONFIG_FLASH_SPI_DW_PROG_TIMEOUT) {
			LOG_ERR("Timeout waiting for flash to clear BUSY bit");
			return -ETIMEDOUT;
		}
	} while (sr & SPI_NOR_WIP_BIT);
	return rc;
}

static int spi_dw_flash_dev_write(const struct device *dev,
				  off_t offset,
				  const void *data,
				  size_t len)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	const struct spi_dw_flash_config *pcfg = cfg->parent_dev->config;
	struct spi_dw_flash_dev_data *dev_data = dev->data;
	uint8_t *buf = (uint8_t *)data;
	uint32_t write_len;
	/* Make sure we reserve bytes in the FIFO for address and opcode */
	const uint32_t fifo_space = pcfg->fifo_depth -
				(sizeof(char) + dev_data->flash_cfg->addr_len);
	int rc;

	rc = spi_dw_lock(cfg->parent_dev);
	if (rc < 0) {
		return rc;
	}

	/*
	 * The Designware SSI controller has an *odd* implementation of
	 * the hardware chip select- the CS line is de-asserted whenever the
	 * TX FIFO is empty. This means that if we encounter interrupt
	 * latency while programming the TX FIFO, CS may be de-asserted early.
	 * To work around this, we only program up to "fifo-depth" bytes
	 * at a time. This means we must split writes into small blocks
	 */

	while (len) {
		write_len = MIN(MIN(len, fifo_space), SPI_NOR_PAGE_SIZE);
		if (((offset % SPI_NOR_PAGE_SIZE) + write_len) > SPI_NOR_PAGE_SIZE) {
			/* Write will cross page boundary, it needs to be split */
			write_len = SPI_NOR_PAGE_SIZE - (offset % SPI_NOR_PAGE_SIZE);
		}
		/* First, set write enable bit */
		rc = spi_dw_prog_extended(cfg->parent_dev,
					  SPI_DW_ACCESS_1_1_1,
					  0, 0);
		if (rc < 0) {
			goto out;
		}
		rc = spi_dw_tx(cfg->parent_dev, SPI_NOR_CMD_WREN, 0, 0,
			       NULL, 0, cfg->cs_idx, cfg->target_freq,
			       SPI_DW_ACCESS_1_1_1);
		if (rc < 0) {
			goto out;
		}

		/* Now write to the flash */
		rc = spi_dw_prog_extended(cfg->parent_dev,
					  dev_data->flash_cfg->mode,
					  dev_data->flash_cfg->addr_len,
					  0);
		if (rc < 0) {
			goto out;
		}
		rc = spi_dw_tx(cfg->parent_dev, dev_data->flash_cfg->pp_cmd,
			       offset, dev_data->flash_cfg->addr_len,
			       buf, write_len, cfg->cs_idx,
			       cfg->target_freq, dev_data->flash_cfg->mode);
		if (rc < 0) {
			goto out;
		}

		/* Wait for flash to clear BUSY bit in status register */
		rc = spi_dw_flash_wait_idle(dev);
		if (rc < 0) {
			goto out;
		}

		offset += write_len;
		buf += write_len;
		len -= write_len;
	}
out:
	spi_dw_unlock(cfg->parent_dev);
	return rc;
}

static int spi_dw_flash_dev_erase(const struct device *dev,
				  off_t offset,
				  size_t size)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	struct spi_dw_flash_dev_data *data = dev->data;
	int rc;
	uint32_t erase_size;
	uint8_t erase_opcode;

	/* erase area must be subregion of device */
	if ((offset < 0) || ((size + offset) > data->flash_size)) {
		return -EINVAL;
	}
	/* Address must be sector aligned */
	if (offset % data->flash_cfg->ssize) {
		return -EINVAL;
	}
	/* Size must be a multiple of sector size */
	if (size % data->flash_cfg->ssize) {
		return -EINVAL;
	}

	rc = spi_dw_lock(cfg->parent_dev);
	if (rc < 0) {
		return rc;
	}

	while (size > 0) {
		/* First, set write enable bit */
		rc = spi_dw_prog_extended(cfg->parent_dev,
					  SPI_DW_ACCESS_1_1_1,
					  0, 0);
		if (rc < 0) {
			goto out;
		}
		rc = spi_dw_tx(cfg->parent_dev, SPI_NOR_CMD_WREN, 0, 0,
			       NULL, 0, cfg->cs_idx, cfg->target_freq,
			       SPI_DW_ACCESS_1_1_1);
		if (rc < 0) {
			goto out;
		}
		if (size == data->flash_size) {
			/* Chip erase */
			rc = spi_dw_prog_extended(cfg->parent_dev,
						  SPI_DW_ACCESS_1_1_1,
						  0, 0);
			if (rc < 0) {
				goto out;
			}
			rc = spi_dw_tx(cfg->parent_dev, data->flash_cfg->ce_cmd,
				       0, 0, NULL, 0, cfg->cs_idx,
				       cfg->target_freq, SPI_DW_ACCESS_1_1_1);
			if (rc < 0) {
				goto out;
			}
			erase_size = data->flash_size;
		} else {
			if (((size % data->flash_cfg->bsize) == 0) &&
			    ((offset % data->flash_cfg->bsize) == 0)) {
				/* Use block erase */
				erase_size = data->flash_cfg->bsize;
				erase_opcode = data->flash_cfg->be_cmd;
			} else {
				/* Sector erase */
				erase_size = data->flash_cfg->ssize;
				erase_opcode = data->flash_cfg->se_cmd;
			}

			rc = spi_dw_prog_extended(cfg->parent_dev,
						  SPI_DW_ACCESS_1_1_1,
						  data->flash_cfg->addr_len, 0);
			if (rc < 0) {
				goto out;
			}
			rc = spi_dw_tx(cfg->parent_dev, erase_opcode, offset,
				       data->flash_cfg->addr_len, NULL, 0,
				       cfg->cs_idx, cfg->target_freq,
				       SPI_DW_ACCESS_1_1_1);
			if (rc < 0) {
				goto out;
			}
		}

		/* Wait for flash to clear BUSY bit in status register */
		rc = spi_dw_flash_wait_idle(dev);
		if (rc < 0) {
			goto out;
		}

		size -= erase_size;
		offset += erase_size;
	}
out:
	spi_dw_unlock(cfg->parent_dev);
	return rc;
}

static int spi_dw_flash_dev_get_size(const struct device *dev, uint64_t *size)
{
	struct spi_dw_flash_dev_data *data = dev->data;

	*size = data->flash_size;
	return 0;
}

static const struct flash_parameters
*spi_dw_flash_dev_get_parameters(const struct device *dev)
{
	ARG_UNUSED(dev);
	return &flash_nor_parameters;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void spi_dw_flash_dev_pages_layout(const struct device *dev,
					  const struct flash_pages_layout **layout,
					  size_t *layout_size)
{
	struct spi_dw_flash_dev_data *data = dev->data;

	*layout = &data->layout;
	*layout_size = 1;
}
#endif

#if defined(CONFIG_FLASH_EX_OP_ENABLED)
static int spi_dw_flash_ex_op(const struct device *dev, uint16_t code,
			      const uintptr_t in, void *out)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;

	if (code == FLASH_EX_OP_SPI_DW_RX_DLY) {
		write_rx_sample_dly(cfg->parent_dev, in);
		return 0;
	}
	return -ENOTSUP;
}
#endif

static int spi_dw_flash_dev_read_jedec_id(const struct device *dev,
					  uint8_t *id)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	int rc;

	rc = spi_dw_lock(cfg->parent_dev);
	if (rc < 0) {
		return rc;
	}

	rc = spi_dw_prog_extended(cfg->parent_dev,
				  SPI_DW_ACCESS_1_1_1,
				  3, 0);
	if (rc < 0) {
		goto out;
	}

	/* Probe JEDEC ID at 20 MHz to be conservative */
	rc = spi_dw_eeprom_transceive(cfg->parent_dev,
				      SPI_NOR_CMD_RDID, 0, 0,
				      id, 3, cfg->cs_idx,
				      MHZ(20), SPI_DW_ACCESS_1_1_1);
out:
	spi_dw_unlock(cfg->parent_dev);
	return rc;
}

static int spi_dw_flash_dev_sfdp_read(const struct device *dev, off_t offset,
				      void *data, size_t len)
{
	const struct spi_dw_flash_dev_config *cfg = dev->config;
	int rc;

	rc = spi_dw_lock(cfg->parent_dev);
	if (rc < 0) {
		return rc;
	}

	rc = spi_dw_prog_extended(cfg->parent_dev,
				  SPI_DW_ACCESS_1_1_1,
				  4, 0);
	if (rc < 0) {
		goto out;
	}

	/* Read SFDP at 20 MHz to be conservative */
	rc = spi_dw_eeprom_transceive(cfg->parent_dev,
				      JESD216_CMD_READ_SFDP, offset << 8, 4,
				      data, len, cfg->cs_idx,
				      MHZ(20), SPI_DW_ACCESS_1_1_1);
out:
	spi_dw_unlock(cfg->parent_dev);
	return rc;
}

static int spi_dw_flash_dev_process_sfdp(const struct device *dev,
					 const struct jesd216_sfdp_header *hp)
{
	struct spi_dw_flash_dev_data *data = dev->data;
	const struct jesd216_param_header *php = hp->phdr;
	int rc;
	uint16_t id = jesd216_param_id(php);
	union {
		uint32_t dw[MIN(php->len_dw, 20)];
		struct jesd216_bfp bfp;
	} u_param;

	if (id != JESD216_SFDP_PARAM_ID_BFP) {
		LOG_ERR("SFDP BFP not found");
		return -EINVAL;
	}

	rc = spi_dw_flash_dev_sfdp_read(dev, jesd216_param_addr(php),
					&u_param.dw, sizeof(u_param.dw));
	if (rc < 0) {
		return rc;
	}

	/* Read BFP DW2 to get flash size */
	data->flash_size = jesd216_bfp_density(&u_param.bfp) / 8;
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	data->layout.pages_count = data->flash_size / data->flash_cfg->ssize;
	data->layout.pages_size = data->flash_cfg->ssize;
#endif
	LOG_DBG("Flash size: %u bytes", data->flash_size);
	return 0;
}

static int spi_dw_flash_init(const struct device *dev)
{
	const struct spi_dw_flash_config *cfg = dev->config;
	struct spi_dw_flash_data *data = dev->data;
	uint32_t ctrlr0 = 0;

#ifdef CONFIG_PINCTRL
	pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
#endif
	/* Mask interrupts, make sure controller is disabled */
	write_imr(dev, DW_SPI_IMR_MASK);
	clear_bit_ssienr(dev);

	cfg->config_func();

	/* Configure SPI DFS for 8 bit frames */
	if (cfg->max_xfer_size == 32) {
		ctrlr0 |= DW_SPI_CTRLR0_DFS_32(8);
	} else {
		ctrlr0 |= DW_SPI_CTRLR0_DFS_16(8);
	}

	write_ctrlr0(dev, ctrlr0);

	k_sem_init(&data->isr_sem, 0, 1);
	k_sem_init(&data->bus_lock, 1, 1);
	return 0;
}

static int spi_dw_flash_dev_init(const struct device *dev)
{
	struct spi_dw_flash_dev_data *data = dev->data;
	int rc, i;
	uint32_t jedec_id;
	const uint8_t decl_nph = 2;
	union {
		/* We only process BFP so use one parameter block */
		uint8_t raw[JESD216_SFDP_SIZE(decl_nph)];
		struct jesd216_sfdp_header sfdp;
	} u_header;
	const struct jesd216_sfdp_header *hp = &u_header.sfdp;

	rc = spi_dw_flash_dev_read_jedec_id(dev, (uint8_t *)&jedec_id);
	if (rc < 0) {
		LOG_ERR("JEDEC ID probe failed: %d", rc);
		return rc;
	}

	jedec_id = sys_be32_to_cpu(jedec_id) >> 8;
	for (i = 0; i < ARRAY_SIZE(flash_devs); i++) {
		if (jedec_id == flash_devs[i].jedec_id) {
			data->flash_cfg = &flash_devs[i].flash;
			LOG_DBG("Found flash with JEDEC ID 0x%03X", jedec_id);
			break;
		}
	}

	if (i == ARRAY_SIZE(flash_devs)) {
		LOG_DBG("Unknown flash, falling back to default settings");
		data->flash_cfg =  &fallback_flash;
	}

	rc = spi_dw_flash_dev_sfdp_read(dev, 0, u_header.raw,
					sizeof(u_header.raw));
	if (rc < 0) {
		LOG_ERR("SFDP read failed: %d", rc);
		return rc;
	}

	if (jesd216_sfdp_magic(hp) != JESD216_SFDP_MAGIC) {
		LOG_ERR("SFDP magic invalid");
		return -EINVAL;
	}

	LOG_INF("%s: SFDP v %u.%u AP %x with %u PH", dev->name,
		hp->rev_major, hp->rev_minor, hp->access, 1 + hp->nph);

	/* Process DW2 to get the flash size */
	return spi_dw_flash_dev_process_sfdp(dev, hp);
}

static struct flash_driver_api dw_spi_flash_dev_api = {
	.read = spi_dw_flash_dev_read,
	.write = spi_dw_flash_dev_write,
	.erase = spi_dw_flash_dev_erase,
	.get_size = spi_dw_flash_dev_get_size,
	.get_parameters = spi_dw_flash_dev_get_parameters,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = spi_dw_flash_dev_pages_layout,
#endif
#if defined(CONFIG_FLASH_JESD216_API)
	.read_jedec_id = spi_dw_flash_dev_read_jedec_id,
	.sfdp_read = spi_dw_flash_dev_sfdp_read,
#endif
#if defined(CONFIG_FLASH_EX_OP_ENABLED)
	.ex_op = spi_dw_flash_ex_op,
#endif
};

#define SPI_CFG_IRQS_SINGLE_ERR_LINE(inst)					\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, rx_avail),		\
			    DT_INST_IRQ_BY_NAME(inst, rx_avail, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, tx_req),		        \
			    DT_INST_IRQ_BY_NAME(inst, tx_req, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, err_int),		\
			    DT_INST_IRQ_BY_NAME(inst, err_int, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, rx_avail));		\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, tx_req));		        \
		irq_enable(DT_INST_IRQN_BY_NAME(inst, err_int));

#define SPI_CFG_IRQS_MULTIPLE_ERR_LINES(inst)					\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, rx_avail),		\
			    DT_INST_IRQ_BY_NAME(inst, rx_avail, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, tx_req),		        \
			    DT_INST_IRQ_BY_NAME(inst, tx_req, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, txo_err),		\
			    DT_INST_IRQ_BY_NAME(inst, txo_err, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, rxo_err),		\
			    DT_INST_IRQ_BY_NAME(inst, rxo_err, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, rxu_err),		\
			    DT_INST_IRQ_BY_NAME(inst, rxu_err, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		IRQ_CONNECT(DT_INST_IRQN_BY_NAME(inst, mst_err),		\
			    DT_INST_IRQ_BY_NAME(inst, mst_err, priority),	\
			    spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),		\
			    0);							\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, rx_avail));		\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, tx_req));		        \
		irq_enable(DT_INST_IRQN_BY_NAME(inst, txo_err));		\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, rxo_err));		\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, rxu_err));		\
		irq_enable(DT_INST_IRQN_BY_NAME(inst, mst_err));

#define SPI_DW_IRQ_HANDLER(inst)                                   \
void spi_dw_irq_config_##inst(void)                                \
{                                                                  \
COND_CODE_1(IS_EQ(DT_NUM_IRQS(DT_DRV_INST(inst)), 1),              \
	(IRQ_CONNECT(DT_INST_IRQN(inst),                           \
		DT_INST_IRQ(inst, priority),                       \
		spi_dw_flash_isr, DEVICE_DT_INST_GET(inst),        \
		0);                                                \
	irq_enable(DT_INST_IRQN(inst));),                          \
	(COND_CODE_1(IS_EQ(DT_NUM_IRQS(DT_DRV_INST(inst)), 3),     \
		(SPI_CFG_IRQS_SINGLE_ERR_LINE(inst)),		   \
		(SPI_CFG_IRQS_MULTIPLE_ERR_LINES(inst)))))	   \
}

#define SPI_DW_INIT(inst)                                                                   \
	IF_ENABLED(CONFIG_PINCTRL, (PINCTRL_DT_INST_DEFINE(inst);))                         \
	SPI_DW_IRQ_HANDLER(inst);                                                           \
	static struct spi_dw_flash_data spi_dw_data_##inst;                                 \
	static const struct spi_dw_flash_config spi_dw_config_##inst = {                    \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(inst)),                                    \
		.clock_frequency = COND_CODE_1(                                             \
			DT_NODE_HAS_PROP(DT_INST_PHANDLE(inst, clocks), clock_frequency),   \
			(DT_INST_PROP_BY_PHANDLE(inst, clocks, clock_frequency)),           \
			(DT_INST_PROP(inst, clock_frequency))),                             \
		.config_func = spi_dw_irq_config_##inst,                                    \
		.serial_target = DT_INST_PROP(inst, serial_target),                         \
		.fifo_depth = DT_INST_PROP(inst, fifo_depth),                               \
		.max_xfer_size = DT_INST_PROP(inst, max_xfer_size),                         \
		IF_ENABLED(CONFIG_PINCTRL, (.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),)) \
	};                                                                                  \
	DEVICE_DT_INST_DEFINE(inst,                                                         \
		spi_dw_flash_init,                                                          \
		NULL,                                                                       \
		&spi_dw_data_##inst,                                                        \
		&spi_dw_config_##inst,                                                      \
		POST_KERNEL,                                                                \
		CONFIG_FLASH_INIT_PRIORITY,                                                 \
		NULL);

DT_INST_FOREACH_STATUS_OKAY(SPI_DW_INIT)

/*
 * The below macros define all SPI flash devices for the SPI controller.
 * The SPI flash devices actually implement the flash driver API, and
 * use the helper functions with their parent controller device to perform
 * flash operations.
 */
#undef DT_DRV_COMPAT
#define DT_DRV_COMPAT snps_designware_spi_flash

#define SPI_DW_DEVICE_INIT(inst)                                               \
	static struct spi_dw_flash_dev_data spi_dw_dev_data_##inst;            \
	static const struct spi_dw_flash_dev_config spi_dw_dev_config_##inst = { \
		.parent_dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),             \
		.target_freq = DT_INST_PROP(inst, spi_max_frequency),          \
		.cs_idx = DT_INST_REG_ADDR(inst),                              \
	};                                                                     \
	DEVICE_DT_INST_DEFINE(inst,                                            \
			      spi_dw_flash_dev_init,                           \
			      NULL,                                            \
			      &spi_dw_dev_data_##inst,                         \
			      &spi_dw_dev_config_##inst,                       \
			      POST_KERNEL,                                     \
			      CONFIG_FLASH_SPI_DW_DEV_INIT_PRIO,               \
			      &dw_spi_flash_dev_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_DW_DEVICE_INIT)
