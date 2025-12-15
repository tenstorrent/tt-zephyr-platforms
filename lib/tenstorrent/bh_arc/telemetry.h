/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @defgroup telemetry Telemetry
 * @brief Telemetry definitions and functions
 *
 * @ref telemetry_tags "Telemetry Tags" describes the various telemetry indices the SMC firmawre
 * currently supports. @ref telemetry_table "Telemetry Table" provides telemetry tracking
 * infrastructure related information.
 *
 */

/** @brief  The current version of the tenstorrent telemetry interface
 * v0.1.0 - Only update when redefining the meaning of an existing tag
 * Semver format: 0 x 00 Major Minor Patch
 */
#define TELEMETRY_VERSION 0x00000100

/**
 * @defgroup telemetry_tags Telemetry Tags
 * @ingroup telemetry
 * @brief Telemetry tag definitions
 * @{
 */

/** @brief High part of the board ID. */
#define TAG_BOARD_ID_HIGH 1

/** @brief Low part of the board ID. */
#define TAG_BOARD_ID_LOW 2

/** @brief ASIC ID. */
#define TAG_ASIC_ID 3

/** @brief Harvesting state of the system. */
#define TAG_HARVESTING_STATE 4

/** @brief Update interval for telemetry in milliseconds. */
#define TAG_UPDATE_TELEM_SPEED 5

/** @brief VCore voltage in millivolts. */
#define TAG_VCORE 6

/** @brief Thermal design power (TDP) in watts. */
#define TAG_TDP 7

/** @brief Thermal design current (TDC) in amperes. */
#define TAG_TDC 8

/** @brief VDD limits (min and max) in millivolts. */
#define TAG_VDD_LIMITS 9

/** @brief Thermal shutdown limit in degrees Celsius. */
#define TAG_THM_LIMIT_SHUTDOWN 10

/** @brief ASIC temperature in signed 16.16 fixed-point format. */
#define TAG_ASIC_TEMPERATURE 11

/** @brief Voltage regulator temperature in degrees Celsius. (Not implemented) */
#define TAG_VREG_TEMPERATURE 12

/** @brief Board temperature in degrees Celsius. (Not implemented) */
#define TAG_BOARD_TEMPERATURE 13

/** @brief AI clock frequency in megahertz. */
#define TAG_AICLK 14

/** @brief AXI clock frequency in megahertz. */
#define TAG_AXICLK 15

/** @brief ARC clock frequency in megahertz. */
#define TAG_ARCCLK 16

/** @brief L2CPU clock 0 frequency in megahertz. */
#define TAG_L2CPUCLK0 17

/** @brief L2CPU clock 1 frequency in megahertz. */
#define TAG_L2CPUCLK1 18

/** @brief L2CPU clock 2 frequency in megahertz. */
#define TAG_L2CPUCLK2 19

/** @brief L2CPU clock 3 frequency in megahertz. */
#define TAG_L2CPUCLK3 20

/** @brief Ethernet live status. */
#define TAG_ETH_LIVE_STATUS 21

/** @brief GDDR status. */
#define TAG_GDDR_STATUS 22

/** @brief GDDR speed in megabits per second. */
#define TAG_GDDR_SPEED 23

/** @brief Ethernet firmware version. */
#define TAG_ETH_FW_VERSION 24

/** @brief GDDR firmware version. */
#define TAG_GDDR_FW_VERSION 25

/** @brief DM application firmware version. */
#define TAG_DM_APP_FW_VERSION 26

/** @brief DM bootloader firmware version. */
#define TAG_DM_BL_FW_VERSION 27

/** @brief Flash bundle version. */
#define TAG_FLASH_BUNDLE_VERSION 28

/** @brief CM firmware version. */
#define TAG_CM_FW_VERSION 29

/** @brief L2CPU firmware version. */
#define TAG_L2CPU_FW_VERSION 30

/** @brief Fan speed as a percentage. */
#define TAG_FAN_SPEED 31

/** @brief Timer heartbeat counter. */
#define TAG_TIMER_HEARTBEAT 32

/** @brief Total number of telemetry tags. */
#define TAG_TELEM_ENUM_COUNT 33

/** @brief Enabled Tensix columns. */
#define TAG_ENABLED_TENSIX_COL 34

/** @brief Enabled Ethernet interfaces. */
#define TAG_ENABLED_ETH 35

/** @brief Enabled GDDR interfaces. */
#define TAG_ENABLED_GDDR 36

/** @brief Enabled L2CPU cores. */
#define TAG_ENABLED_L2CPU 37

/** @brief PCIe usage information. */
#define TAG_PCIE_USAGE 38

/** @brief Input current in amperes. */
#define TAG_INPUT_CURRENT 39

/** @brief NOC translation status. */
#define TAG_NOC_TRANSLATION 40

/** @brief Fan RPM. */
#define TAG_FAN_RPM 41

/** @brief GDDR 0 and 1 temperature. */
#define TAG_GDDR_0_1_TEMP 42

/** @brief GDDR 2 and 3 temperature. */
#define TAG_GDDR_2_3_TEMP 43

/** @brief GDDR 4 and 5 temperature. */
#define TAG_GDDR_4_5_TEMP 44

/** @brief GDDR 6 and 7 temperature. */
#define TAG_GDDR_6_7_TEMP 45

/** @brief GDDR 0 and 1 corrected errors. */
#define TAG_GDDR_0_1_CORR_ERRS 46

/** @brief GDDR 2 and 3 corrected errors. */
#define TAG_GDDR_2_3_CORR_ERRS 47

/** @brief GDDR 4 and 5 corrected errors. */
#define TAG_GDDR_4_5_CORR_ERRS 48

/** @brief GDDR 6 and 7 corrected errors. */
#define TAG_GDDR_6_7_CORR_ERRS 49

/** @brief GDDR uncorrected errors. */
#define TAG_GDDR_UNCORR_ERRS 50

/** @brief Maximum GDDR temperature. */
#define TAG_MAX_GDDR_TEMP 51

/** @brief ASIC location. */
#define TAG_ASIC_LOCATION 52

/** @brief Board power limit in watts. */
#define TAG_BOARD_POWER_LIMIT 53

/** @brief Input power in watts. */
#define TAG_INPUT_POWER 54

/** @brief Maximum TDC limit in amperes. */
#define TAG_TDC_LIMIT_MAX 55

/** @brief Thermal throttle limit in degrees Celsius. */
#define TAG_THM_LIMIT_THROTTLE 56

/** @brief Firmware build date. */
#define TAG_FW_BUILD_DATE 57

/** @brief TT flash version. */
#define TAG_TT_FLASH_VERSION 58

/** @brief Enabled Tensix rows. */
#define TAG_ENABLED_TENSIX_ROW 59

/** @brief Thermal trip count. */
#define TAG_THERM_TRIP_COUNT 60

/** @brief High part of the ASIC ID. */
#define TAG_ASIC_ID_HIGH 61

/** @brief Low part of the ASIC ID. */
#define TAG_ASIC_ID_LOW 62

/** @brief Maximum AI clock frequency. */
#define TAG_AICLK_LIMIT_MAX 63

/** @brief Maximum TDP limit in watts. */
#define TAG_TDP_LIMIT_MAX 64

/**
 * @brief Effective minimum AICLK arbiter value in megahertz.
 *
 * This represents the highest frequency requested by all enabled minimum arbiters.
 * Multiple arbiters may request minimum frequencies, and the highest value is effective.
 */
#define TAG_AICLK_ARB_MIN 65

/**
 * @brief Effective maximum AICLK arbiter value in megahertz.
 *
 * This represents the lowest frequency limit imposed by all enabled maximum arbiters.
 * Multiple arbiters may impose maximum frequency limits (e.g., TDP, TDC, thermal throttling),
 * and the lowest (most restrictive) value is effective. This value takes precedence over
 * TAG_AICLK_ARB_MIN when determining the final target frequency.
 */
#define TAG_AICLK_ARB_MAX 66

/** @} */ /* end of telemetry_tag group */

/* Not a real tag, signifies the last tag in the list.
 * MUST be incremented if new tags are defined.
 */
#define TAG_COUNT 67

/* Telemetry tags are at offset `tag` in the telemetry buffer */
#define TELEM_OFFSET(tag) (tag)

void init_telemetry(uint32_t app_version);
uint32_t ConvertFloatToTelemetry(float value);
float ConvertTelemetryToFloat(int32_t value);
int GetMaxGDDRTemp(void);
void StartTelemetryTimer(void);
void UpdateDmFwVersion(uint32_t bl_version, uint32_t app_version);
void UpdateTelemetryNocTranslation(bool translation_enabled);
void UpdateTelemetryBoardPowerLimit(uint32_t power_limit);
void UpdateTelemetryThermTripCount(uint16_t therm_trip_count);
bool GetTelemetryTagValid(uint16_t tag);
uint32_t GetTelemetryTag(uint16_t tag);

#endif
