/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Heavily based on spi_dw.h, which is:
 * Copyright (c) 2015 Intel Corporation.
 * Copyright (c) 2023 Synopsys, Inc. All rights reserved.
 */

#ifndef ZEPHYR_DRIVERS_FLASH_SPI_DW_FLASH_H
#define ZEPHYR_DRIVERS_FLASH_SPI_DW_FLASH_H

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_PINCTRL
#include <zephyr/drivers/pinctrl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*spi_dw_config_t)(void);

/* Access modes for page program and read commands */
enum spi_dw_access_mode {
	SPI_DW_ACCESS_1_1_1 = 0,
	SPI_DW_ACCESS_1_1_2,
	SPI_DW_ACCESS_1_2_2,
	SPI_DW_ACCESS_2_2_2,
	SPI_DW_ACCESS_1_1_4,
	SPI_DW_ACCESS_1_4_4,
	SPI_DW_ACCESS_4_4_4,
	SPI_DW_ACCESS_1_1_8,
	SPI_DW_ACCESS_1_8_8,
	SPI_DW_ACCESS_8_8_8,
};

/* Tracks settings for interfacing with flash device */
struct spi_dw_flash {
	enum spi_dw_access_mode mode; /* Flash access mode */
	uint8_t addr_len; /* Address length in bytes */
	uint8_t read_cmd; /* Read command */
	uint8_t read_dummy; /* Dummy cycles for read command */
	uint8_t ce_cmd; /* Chip erase command */
	uint8_t se_cmd; /* Sector erase command */
	uint8_t be_cmd; /* Block erase command */
	uint8_t pp_cmd; /* Page program command */
	uint32_t ssize; /* Sector size in bytes */
	uint32_t bsize; /* Block size in bytes */
};

/* Config and data structures for flash devices */
struct spi_dw_flash_dev_config {
	const struct device *parent_dev;
	uint32_t target_freq;
	uint8_t cs_idx;
};

struct spi_dw_flash_dev_data {
	const struct spi_dw_flash *flash_cfg;
	uint32_t flash_size; /* Flash size in bytes */
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	struct flash_pages_layout layout;
#endif
};

/* Config and data structures for SPI controller */
struct spi_dw_flash_config {
	DEVICE_MMIO_ROM;
	uint32_t clock_frequency;
	spi_dw_config_t config_func;
	bool serial_target;
	uint8_t fifo_depth;
	uint8_t max_xfer_size;
#ifdef CONFIG_PINCTRL
	const struct pinctrl_dev_config *pcfg;
#endif
};

struct spi_dw_flash_data {
	struct k_sem isr_sem;
	struct k_sem bus_lock;
	uint8_t *rx_pos;
	uint32_t rx_len;
	uint32_t err_state;
};

/* Entry for flash device settings */
struct spi_dw_flash_entry {
	uint32_t jedec_id; /* JEDEC ID of flash */
	struct spi_dw_flash flash; /* Flash settings */
};

/* Helper macros */

#define SPI_DW_CLK_DIVIDER(clock_freq, ssi_clk_hz) \
		((clock_freq / ssi_clk_hz) & 0xFFFF)

#define DEFINE_MM_REG_READ(__reg, __off, __sz)				\
	static inline uint32_t read_##__reg(const struct device *dev)	\
	{								\
		return sys_read32(DEVICE_MMIO_GET(dev) + __off);        \
	}
#define DEFINE_MM_REG_WRITE(__reg, __off, __sz)				\
	static inline void write_##__reg(const struct device *dev, uint32_t data)\
	{								\
		sys_write32(data, DEVICE_MMIO_GET(dev) + __off);        \
	}

#define DEFINE_SET_BIT_OP(__reg_bit, __reg_off, __bit)			\
	static inline void set_bit_##__reg_bit(const struct device *dev)	\
	{								\
		sys_set_bit(DEVICE_MMIO_GET(dev) + __reg_off, __bit);   \
	}

#define DEFINE_CLEAR_BIT_OP(__reg_bit, __reg_off, __bit)		\
	static inline void clear_bit_##__reg_bit(const struct device *dev)\
	{								\
		sys_clear_bit(DEVICE_MMIO_GET(dev) + __reg_off, __bit); \
	}

#define DEFINE_TEST_BIT_OP(__reg_bit, __reg_off, __bit)			\
	static inline int test_bit_##__reg_bit(const struct device *dev)\
	{								\
		return sys_test_bit(DEVICE_MMIO_GET(dev) + __reg_off, __bit);   \
	}

/* Common registers settings, bits etc... */

/* CTRLR0 settings */
#define DW_SPI_CTRLR0_SCPH_BIT		(6)
#define DW_SPI_CTRLR0_SCPOL_BIT		(7)
#define DW_SPI_CTRLR0_TMOD_SHIFT	(8)
#define DW_SPI_CTRLR0_SLV_OE_BIT	(10)
#define DW_SPI_CTRLR0_SRL_BIT		(11)

#define DW_SPI_CTRLR0_SCPH		BIT(DW_SPI_CTRLR0_SCPH_BIT)
#define DW_SPI_CTRLR0_SCPOL		BIT(DW_SPI_CTRLR0_SCPOL_BIT)
#define DW_SPI_CTRLR0_SRL		BIT(DW_SPI_CTRLR0_SRL_BIT)
#define DW_SPI_CTRLR0_SLV_OE		BIT(DW_SPI_CTRLR0_SLV_OE_BIT)

#define DW_SPI_CTRLR0_TMOD_TX_RX	(0)
#define DW_SPI_CTRLR0_TMOD_TX		(1 << DW_SPI_CTRLR0_TMOD_SHIFT)
#define DW_SPI_CTRLR0_TMOD_RX		(2 << DW_SPI_CTRLR0_TMOD_SHIFT)
#define DW_SPI_CTRLR0_TMOD_EEPROM	(3 << DW_SPI_CTRLR0_TMOD_SHIFT)
#define DW_SPI_CTRLR0_TMOD_RESET	(3 << DW_SPI_CTRLR0_TMOD_SHIFT)

#define DW_SPI_CTRLR0_DFS_16(__bpw)	((__bpw) - 1)
#define DW_SPI_CTRLR0_DFS_32(__bpw)	(((__bpw) - 1) << 16)

/* 0x38 represents the bits 8, 16 and 32. Knowing that 24 is bits 8 and 16
 * These are the bits were when you divide by 8, you keep the result as it is.
 * For all the other ones, 4 to 7, 9 to 15, etc... you need a +1,
 * since on such division it takes only the result above 0
 */
#define SPI_WS_TO_DFS(__bpw)		(((__bpw) & ~0x38) ?		\
					 (((__bpw) / 8) + 1) :		\
					 ((__bpw) / 8))

/* SSIENR bits */
#define DW_SPI_SSIENR_SSIEN_BIT		(0)

/* CLK_ENA bits */
#define DW_SPI_CLK_ENA_BIT		(0)

/* SR bits and values */
#define DW_SPI_SR_BUSY_BIT		(0)
#define DW_SPI_SR_TFNF_BIT		(1)
#define DW_SPI_SR_RFNE_BIT		(3)

/* IMR bits (ISR valid as well) */
#define DW_SPI_IMR_TXEIM_BIT		(0)
#define DW_SPI_IMR_TXOIM_BIT		(1)
#define DW_SPI_IMR_RXUIM_BIT		(2)
#define DW_SPI_IMR_RXOIM_BIT		(3)
#define DW_SPI_IMR_RXFIM_BIT		(4)
#define DW_SPI_IMR_MSTIM_BIT		(5)

/* IMR values */
#define DW_SPI_IMR_TXEIM		BIT(DW_SPI_IMR_TXEIM_BIT)
#define DW_SPI_IMR_TXOIM		BIT(DW_SPI_IMR_TXOIM_BIT)
#define DW_SPI_IMR_RXUIM		BIT(DW_SPI_IMR_RXUIM_BIT)
#define DW_SPI_IMR_RXOIM		BIT(DW_SPI_IMR_RXOIM_BIT)
#define DW_SPI_IMR_RXFIM		BIT(DW_SPI_IMR_RXFIM_BIT)
#define DW_SPI_IMR_MSTIM		BIT(DW_SPI_IMR_MSTIM_BIT)

/* ISR values (same as IMR) */
#define DW_SPI_ISR_TXEIS		DW_SPI_IMR_TXEIM
#define DW_SPI_ISR_TXOIS		DW_SPI_IMR_TXOIM
#define DW_SPI_ISR_RXUIS		DW_SPI_IMR_RXUIM
#define DW_SPI_ISR_RXOIS		DW_SPI_IMR_RXOIM
#define DW_SPI_ISR_RXFIS		DW_SPI_IMR_RXFIM
#define DW_SPI_ISR_MSTIS		DW_SPI_IMR_MSTIM

/* Error interrupt */
#define DW_SPI_ISR_ERRORS_MASK		(DW_SPI_ISR_TXOIS | \
					 DW_SPI_ISR_RXUIS | \
					 DW_SPI_ISR_RXOIS | \
					 DW_SPI_ISR_MSTIS)
/* ICR Bit */
#define DW_SPI_SR_ICR_BIT		(0)

/* Interrupt mask (IMR) */
#define DW_SPI_IMR_MASK			(0x0)
#define DW_SPI_IMR_UNMASK		(DW_SPI_IMR_TXEIM | \
					 DW_SPI_IMR_TXOIM | \
					 DW_SPI_IMR_RXUIM | \
					 DW_SPI_IMR_RXOIM | \
					 DW_SPI_IMR_RXFIM)
#define DW_SPI_IMR_MASK_TX		(~(DW_SPI_IMR_TXEIM | \
					   DW_SPI_IMR_TXOIM))
#define DW_SPI_IMR_MASK_RX		(~(DW_SPI_IMR_RXUIM | \
					   DW_SPI_IMR_RXOIM | \
					   DW_SPI_IMR_RXFIM))
/* Included from drivers/spi */
#include "spi_dw_regs.h"

/* Additional register definitions for extended SPI modes */
#define DW_SPI_REG_RX_SAMPLE_DLY	(0xf0)
#define DW_SPI_REG_SPI_CTRLR0		(0xf4)

#define DW_SPI_CTRLR0_FRF_SHIFT		(21)
#define DW_SPI_CTRLR0_FRF_STD		(0x0 << DW_SPI_CTRLR0_FRF_SHIFT)
#define DW_SPI_CTRLR0_FRF_DUAL		(0x1 << DW_SPI_CTRLR0_FRF_SHIFT)
#define DW_SPI_CTRLR0_FRF_QUAD		(0x2 << DW_SPI_CTRLR0_FRF_SHIFT)
#define DW_SPI_CTRLR0_FRF_OCTAL		(0x3 << DW_SPI_CTRLR0_FRF_SHIFT)
#define DW_SPI_CTRLR0_FRF_RESET		(0x3 << DW_SPI_CTRLR0_FRF_SHIFT)

#define DW_SPI_SPI_CTRLR0_WAIT_CYCLES(x)	(((x) & 0x1F) << 11)
#define DW_SPI_SPI_CTRLR0_INST_L(x)		(((x) & 0x3) << 8)
#define DW_SPI_SPI_CTRLR0_ADDR_L(x)		(((x) & 0xF) << 2)
#define DW_SPI_SPI_CTRLR0_TRANS_TYPE(x)		((x) & 0x3)

/* Based on those macros above, here are common helpers for some registers */
DEFINE_MM_REG_READ(txflr, DW_SPI_REG_TXFLR, 32)
DEFINE_MM_REG_READ(rxflr, DW_SPI_REG_RXFLR, 32)

DEFINE_MM_REG_WRITE(baudr, DW_SPI_REG_BAUDR, 32)
DEFINE_MM_REG_WRITE(imr, DW_SPI_REG_IMR, 32)
DEFINE_MM_REG_WRITE(spi_ctrlr0, DW_SPI_REG_SPI_CTRLR0, 32);
DEFINE_MM_REG_WRITE(rx_sample_dly, DW_SPI_REG_RX_SAMPLE_DLY, 32)
DEFINE_MM_REG_READ(imr, DW_SPI_REG_IMR, 32)
DEFINE_MM_REG_READ(isr, DW_SPI_REG_ISR, 32)
DEFINE_MM_REG_READ(risr, DW_SPI_REG_RISR, 32)

DEFINE_SET_BIT_OP(ssienr, DW_SPI_REG_SSIENR, DW_SPI_SSIENR_SSIEN_BIT)
DEFINE_CLEAR_BIT_OP(ssienr, DW_SPI_REG_SSIENR, DW_SPI_SSIENR_SSIEN_BIT)
DEFINE_TEST_BIT_OP(ssienr, DW_SPI_REG_SSIENR, DW_SPI_SSIENR_SSIEN_BIT)
DEFINE_TEST_BIT_OP(sr_busy, DW_SPI_REG_SR, DW_SPI_SR_BUSY_BIT)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_FLASH_SPI_DW_FLASH_H */
