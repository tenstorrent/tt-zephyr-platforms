/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SMBUS_TARGET
#define SMBUS_TARGET

#include <stdint.h>

/**
 * @brief A list of supported SMBUS transaction types
 */
typedef enum {
	kSmbusTransWriteByte,
	kSmbusTransReadByte,
	kSmbusTransWriteWord,
	kSmbusTransReadWord,
	kSmbusTransBlockWrite,
	kSmbusTransBlockRead,
	kSmbusTransBlockWriteBlockRead
} SmbusTransType;

/**
 * @brief Definition of a SMBUS receive handler
 * @details This function is invoked when the SMBUS target has data from
 *          the I2C controller to relay to the application. SMBUS receive handlers
 *          shall return 0 on success, and any other value on failure.
 */
typedef int32_t (*SmbusRcvHandler)(const uint8_t *data, uint8_t size);

/**
 * @brief Definition of a SMBUS send handler
 * @details This function is invoked when the SMBUS target requests data from the
 *          application to send to the I2C controller. SMBUS send handlers shall
 *          return 0 on success, and any other value on failure.
 */
typedef int32_t (*SmbusSendHandler)(uint8_t *data, uint8_t size);

typedef struct {
	SmbusTransType trans_type;
	SmbusRcvHandler rcv_handler;
	SmbusSendHandler send_handler;
	uint8_t expected_blocksize_r; /* Only used for block r commands */
	uint8_t expected_blocksize_w; /* Only used for block w commands */
	uint8_t pec: 1;
	uint8_t variable_blocksize: 1; /* If set, block size can be <= expected */
} SmbusCmdDef;

/**
 * @brief Register the given command to the SMBUS target implementation
 * @param dev The device to register the command for.
 * @param cmd_id The command ID to register the handler for.
 * @param smbus_cmd Pointer to the smbus command to register. The memory must have
 *                  persistent storage for the duration of the time the target remains
 *                  registered to the I2C.
 */
int32_t smbus_target_register_cmd(const struct device *dev, uint8_t cmd_id,
				  const SmbusCmdDef *smbus_cmd);
#endif
