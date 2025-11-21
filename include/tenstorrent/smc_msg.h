/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file smc_msg.h
 * @brief Tenstorrent host command IDs
 */

#ifndef TT_SMC_MSG__H_
#define TT_SMC_MSG__H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup tt_msg_apis
 * @{
 */

/** @brief Enumeration listing the available host requests IDs the SMC can process*/
enum tt_smc_msg {
	/** @brief Reserved*/
	TT_SMC_MSG_RESERVED_01 = 0x1,

	/** @brief No-op request (not supported) */
	TT_SMC_MSG_NOP = 0x11,

	/** @brief @ref set_voltage_rqst "Set voltage request" */
	TT_SMC_MSG_SET_VOLTAGE = 0x12,

	/** @brief @ref get_voltage_rqst "Get voltage request" */
	TT_SMC_MSG_GET_VOLTAGE = 0x13,

	/** @brief @ref switch_clk_scheme_rqst "Switch clock scheme request" */
	TT_SMC_MSG_SWITCH_CLK_SCHEME = 0x14,

	/** @brief @ref debug_noc_translation_rqst "Debug NOC translation request" */
	TT_SMC_MSG_DEBUG_NOC_TRANSLATION = 0x15,
	TT_SMC_MSG_REPORT_SCRATCH_ONLY = 0x16,

	/** @brief @ref send_pcie_msi_rqst "Send PCIe MSI request" */
	TT_SMC_MSG_SEND_PCIE_MSI = 0x17,

	/** @brief @ref switch_vout_control_rqst "Switch VOUT control request" */
	TT_SMC_MSG_SWITCH_VOUT_CONTROL = 0x18,

	TT_SMC_MSG_READ_EEPROM = 0x19,
	TT_SMC_MSG_WRITE_EEPROM = 0x1A,
	TT_SMC_MSG_READ_TS = 0x1B,
	TT_SMC_MSG_READ_PD = 0x1C,
	TT_SMC_MSG_READ_VM = 0x1D,
	TT_SMC_MSG_I2C_MESSAGE = 0x1E,
	/** @brief eFuse burn bits request (not supported) */
	TT_SMC_MSG_EFUSE_BURN_BITS = 0x1F,
	TT_SMC_MSG_REINIT_TENSIX = 0x20,
	/** @brief @ref power_setting_rqst "Power Setting Request"*/
	TT_SMC_MSG_POWER_SETTING = 0x21,
	/** @brief @ref get_freq_curve_from_voltage_rqst "Frequency Curve from Voltage Request"*/
	TT_SMC_MSG_GET_FREQ_CURVE_FROM_VOLTAGE = 0x30,
	TT_SMC_MSG_AISWEEP_START = 0x31,
	TT_SMC_MSG_AISWEEP_STOP = 0x32,
	TT_SMC_MSG_FORCE_AICLK = 0x33,
	TT_SMC_MSG_GET_AICLK = 0x34,
	TT_SMC_MSG_FORCE_VDD = 0x39,
	/** @brief PCIe index request (not supported) */
	TT_SMC_MSG_PCIE_INDEX = 0x51,
	/** @brief @ref aiclk_set_speed_rqst "AI Clock Set Busy Speed Request"*/
	TT_SMC_MSG_AICLK_GO_BUSY = 0x52,
	/** @brief @ref aiclk_set_speed_rqst "AI Clock Set Idle Speed Request"*/
	TT_SMC_MSG_AICLK_GO_LONG_IDLE = 0x54,
	/* arg: 3 = ASIC + M3 reset, other values = ASIC-only reset */
	TT_SMC_MSG_TRIGGER_RESET = 0x56,

	/** @brief Reserved*/
	TT_SMC_MSG_RESERVED_60 = 0x60,
	TT_SMC_MSG_TEST = 0x90,
	TT_SMC_MSG_PCIE_DMA_CHIP_TO_HOST_TRANSFER = 0x9B,
	TT_SMC_MSG_PCIE_DMA_HOST_TO_CHIP_TRANSFER = 0x9C,
	/** @brief PCIe error count reset request (not supported) */
	TT_SMC_MSG_PCIE_ERROR_CNT_RESET = 0x9D,
	/** @brief Trigger IRQ request (not supported) */
	TT_SMC_MSG_TRIGGER_IRQ = 0x9F,
	TT_SMC_MSG_ASIC_STATE0 = 0xA0,
	/** @brief ASIC state 1 request (not supported) */
	TT_SMC_MSG_ASIC_STATE1 = 0xA1,
	TT_SMC_MSG_ASIC_STATE3 = 0xA3,
	/** @brief ASIC state 5 request (not supported) */
	TT_SMC_MSG_ASIC_STATE5 = 0xA5,
	/** @brief @ref get_voltage_curve_from_freq_rqst "Voltage Curve from Frequency Request"*/
	TT_SMC_MSG_GET_VOLTAGE_CURVE_FROM_FREQ = 0xA6,

	/** @brief @ref force_fan_speed_rqst "Force Fan Speed Request"*/
	TT_SMC_MSG_FORCE_FAN_SPEED = 0xAC,
	/** @brief Get DRAM temperature request (not supported) */
	TT_SMC_MSG_GET_DRAM_TEMPERATURE = 0xAD,
	TT_SMC_MSG_TOGGLE_TENSIX_RESET = 0xAF,
	/** @brief DRAM BIST start request (not supported) */
	TT_SMC_MSG_DRAM_BIST_START = 0xB0,
	/** @brief NOC write word request (not supported) */
	TT_SMC_MSG_NOC_WRITE_WORD = 0xB1,
	/** @brief Toggle Ethernet reset request (not supported) */
	TT_SMC_MSG_TOGGLE_ETH_RESET = 0xB2,
	/** @brief Set DRAM refresh rate request (not supported) */
	TT_SMC_MSG_SET_DRAM_REFRESH_RATE = 0xB3,
	/** @brief ARC DMA request (not supported) */
	TT_SMC_MSG_ARC_DMA = 0xB4,
	/** @brief Test SPI request (not supported) */
	TT_SMC_MSG_TEST_SPI = 0xB5,
	/** @brief Current date request (not supported) */
	TT_SMC_MSG_CURR_DATE = 0xB7,
	/** @brief Update M3 auto reset timeout request (not supported) */
	TT_SMC_MSG_UPDATE_M3_AUTO_RESET_TIMEOUT = 0xBC,
	/** @brief Clear number of auto resets request (not supported) */
	TT_SMC_MSG_CLEAR_NUM_AUTO_RESET = 0xBD,
	TT_SMC_MSG_SET_LAST_SERIAL = 0xBE,
	/** @brief eFuse burn request (not supported) */
	TT_SMC_MSG_EFUSE_BURN = 0xBF,
	/** @brief Ping DMC request */
	TT_SMC_MSG_PING_DM = 0xC0,
	TT_SMC_MSG_SET_WDT_TIMEOUT = 0xC1,
	/** @brief Flash write unlock request */
	TT_SMC_MSG_FLASH_UNLOCK = 0xC2,
	/** @brief Flash write lock request */
	TT_SMC_MSG_FLASH_LOCK = 0xC3,
	/** @brief Confirm SPI flash succeeded */
	TT_SMC_MSG_CONFIRM_FLASHED_SPI = 0xC4,
	/** @brief Hotload request */
	TT_SMC_MSG_HOTLOAD = 0xC5,
};

/** @} */

#ifdef __cplusplus
}
#endif

#endif
