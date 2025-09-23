/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file msg_type.h
 * @brief Tenstorrent host command IDs
 */

#ifndef TENSTORRENT_MSG_TYPE_H_
#define TENSTORRENT_MSG_TYPE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup tt_msg_apis
 * @{
 */

/** @brief Enumeration listing the available host requests IDs the SMC can process*/
enum msg_type {
	/** @brief Reserved*/
	MSG_TYPE_RESERVED_01 = 0x1,

	/** @brief No-op request*/
	MSG_TYPE_NOP = 0x11,

	MSG_TYPE_SET_VOLTAGE = 0x12,
	MSG_TYPE_GET_VOLTAGE = 0x13,
	MSG_TYPE_SWITCH_CLK_SCHEME = 0x14,
	MSG_TYPE_DEBUG_NOC_TRANSLATION = 0x15,
	MSG_TYPE_REPORT_SCRATCH_ONLY = 0x16,
	MSG_TYPE_SEND_PCIE_MSI = 0x17,
	MSG_TYPE_SWITCH_VOUT_CONTROL = 0x18,
	MSG_TYPE_READ_EEPROM = 0x19,
	MSG_TYPE_WRITE_EEPROM = 0x1A,
	MSG_TYPE_READ_TS = 0x1B,
	MSG_TYPE_READ_PD = 0x1C,
	MSG_TYPE_READ_VM = 0x1D,
	MSG_TYPE_I2C_MESSAGE = 0x1E,
	MSG_TYPE_EFUSE_BURN_BITS = 0x1F,
	MSG_TYPE_REINIT_TENSIX = 0x20,
	MSG_TYPE_GET_FREQ_CURVE_FROM_VOLTAGE = 0x30,
	MSG_TYPE_AISWEEP_START = 0x31,
	MSG_TYPE_AISWEEP_STOP = 0x32,
	MSG_TYPE_FORCE_AICLK = 0x33,
	MSG_TYPE_GET_AICLK = 0x34,
	MSG_TYPE_FORCE_VDD = 0x39,
	MSG_TYPE_PCIE_INDEX = 0x51,
	/** @brief @ref aiclk_set_speed_rqst_t "AI Clock Set Busy Speed Request"*/
	MSG_TYPE_AICLK_GO_BUSY = 0x52,
	/** @brief @ref aiclk_set_speed_rqst_t "AI Clock Set Idle Speed Request"*/
	MSG_TYPE_AICLK_GO_LONG_IDLE = 0x54,
	MSG_TYPE_TRIGGER_RESET =
		0x56, /* arg: 3 = ASIC + M3 reset, other values = ASIC-only reset */

	/** @brief Reserved*/
	MSG_TYPE_RESERVED_60 = 0x60,
	MSG_TYPE_TEST = 0x90,
	MSG_TYPE_PCIE_DMA_CHIP_TO_HOST_TRANSFER = 0x9B,
	MSG_TYPE_PCIE_DMA_HOST_TO_CHIP_TRANSFER = 0x9C,
	MSG_TYPE_PCIE_ERROR_CNT_RESET = 0x9D,
	MSG_TYPE_TRIGGER_IRQ = 0x9F,
	MSG_TYPE_ASIC_STATE0 = 0xA0,
	MSG_TYPE_ASIC_STATE1 = 0xA1,
	MSG_TYPE_ASIC_STATE3 = 0xA3,
	MSG_TYPE_ASIC_STATE5 = 0xA5,
	MSG_TYPE_GET_VOLTAGE_CURVE_FROM_FREQ = 0xA6,

	/** @brief @ref force_fan_speed_rqst_t "Force Fan Speed Request"*/
	MSG_TYPE_FORCE_FAN_SPEED = 0xAC,
	MSG_TYPE_GET_DRAM_TEMPERATURE = 0xAD,
	MSG_TYPE_TOGGLE_TENSIX_RESET = 0xAF,
	MSG_TYPE_DRAM_BIST_START = 0xB0,
	MSG_TYPE_NOC_WRITE_WORD = 0xB1, /* additional parameters passed in ARC_MAILBOX */
	MSG_TYPE_TOGGLE_ETH_RESET = 0xB2,
	MSG_TYPE_SET_DRAM_REFRESH_RATE = 0xB3,
	MSG_TYPE_ARC_DMA = 0xB4,
	MSG_TYPE_TEST_SPI = 0xB5,
	MSG_TYPE_CURR_DATE = 0xB7,
	MSG_TYPE_UPDATE_M3_AUTO_RESET_TIMEOUT = 0xBC,
	MSG_TYPE_CLEAR_NUM_AUTO_RESET = 0xBD,
	MSG_TYPE_SET_LAST_SERIAL = 0xBE,
	MSG_TYPE_EFUSE_BURN = 0xBF,
	MSG_TYPE_PING_DM = 0xC0,
	MSG_TYPE_SET_WDT_TIMEOUT = 0xC1,
};

/** @} */

#ifdef __cplusplus
}
#endif

#endif
