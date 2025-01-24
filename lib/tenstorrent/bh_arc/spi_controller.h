/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SPI_CONTROLLER_H
#define SPI_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#define SPI_TX_FIFO_DEPTH 16
#define SPI_RX_FIFO_DEPTH 256

typedef enum {
	SpiStandardMode = 0,
	SpiDualMode = 1,
	SpiQuadMode = 2,
	SpiOctalMode = 3,
} SpiIoMode;

void SpiProgramWaitCyclesAndAddrl(uint8_t wait_cycles, uint8_t addr_width);
void SpiControllerModeSetup(bool ddr, SpiIoMode io_mode);
void SpiControllerClkSetup(bool ddr);
void SpiDetectOpMode(bool *ddr, SpiIoMode *io_mode, uint8_t *addr_width);
void EepromTransmit(uint32_t *tx_data, uint32_t num_frames);
void EepromRead(uint32_t *tx_data, uint32_t num_write_frames, uint8_t *rx_data,
		uint32_t num_read_frames);
void SpiControllerReset(void);
void SetRxSampleDelay(uint32_t delay);
#endif
