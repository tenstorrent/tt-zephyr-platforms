/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include "dw_apb_i2c.h"

#define DATA_TOO_LARGE 0x01

#define BYTE_GET(v, b) FIELD_GET(0xFFu << ((b) * 8), (v))

/*
 * Response Buffer
 * |   | 0            | 1           | 2        | 3             |
 * |---|--------------|-------------|----------|---------------|
 * | 0 | status       | unused      | unused   | unused        |
 * | 1 | Read Data (28B)                                       |
 * | 2 |                                                       |
 * | 3 |                                                       |
 * | 4 |                                                       |
 * | 5 |                                                       |
 * | 6 |                                                       |
 * | 7 |                                                       |
 */

/**
 * @brief Handler for TT_SMC_MSG_I2C_MESSAGE messages
 *
 * @details Performs I2C read/write transactions. The message can contain both write
 *          and read operations in a single transaction.
 *
 * @param request Pointer to the host request message, use request->i2c_message for structured
 *                access
 * @param response Pointer to the response message to be sent back to host, will contain:
 *                 - Read data if read operation was requested
 *
 * @return 0 on success
 * @return non-zero on error
 *
 * @see i2c_message_rqst_t
 */
static uint8_t i2c_message_handler(const union request *request, struct response *response)
{
	uint8_t I2C_mst_id = request->i2c_message.i2c_mst_id;
	bool valid_id = IsValidI2CMasterId(I2C_mst_id);

	if (!valid_id) {
		return !valid_id;
	}
	uint8_t I2C_slave_address = request->i2c_message.i2c_slave_address & 0x7F;
	uint8_t num_write_bytes = request->i2c_message.num_write_bytes;
	uint8_t num_read_bytes = request->i2c_message.num_read_bytes;

	size_t remaining_write_size = sizeof(request->i2c_message.write_data);
	size_t remaining_read_size = sizeof(response->data) - 1 * sizeof(response->data[0]);

	if (num_write_bytes > remaining_write_size || num_read_bytes > remaining_read_size) {
		return DATA_TOO_LARGE;
	}

	uint8_t *write_data_ptr = (uint8_t *)request->i2c_message.write_data;
	uint8_t *read_data_ptr = (uint8_t *)&response->data[1];

	I2CInit(I2CMst, I2C_slave_address, I2CStandardMode, I2C_mst_id);
	uint32_t status = I2CTransaction(I2C_mst_id, write_data_ptr, num_write_bytes, read_data_ptr,
					 num_read_bytes);

	return status != 0;
}

REGISTER_MESSAGE(TT_SMC_MSG_I2C_MESSAGE, i2c_message_handler);
