/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>
#include "dw_apb_i2c.h"

#define DATA_TOO_LARGE 0x01

#define BYTE_GET(v, b) FIELD_GET(0xFFu << ((b) * 8), (v))

/*
 * Request Buffer
 * |   | 0            | 1           | 2        | 3             |
 * |---|--------------|-------------|----------|---------------|
 * | 0 | MSG          | I2C Line ID | Slave ID | # write bytes |
 * | 1 | # read bytes | unused      | unused   | unused        |
 * | 2 | Write Data (24B)                                      |
 * | 3 |                                                       |
 * | 4 |                                                       |
 * | 5 |                                                       |
 * | 6 |                                                       |
 * | 7 |                                                       |
 *
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

static uint8_t i2c_message_handler(uint32_t msg_code, const struct request *request,
				   struct response *response)
{
	uint8_t I2C_mst_id = BYTE_GET(request->data[0], 1);
	bool valid_id = IsValidI2CMasterId(I2C_mst_id);

	if (!valid_id) {
		return !valid_id;
	}
	uint8_t I2C_slave_address =
		BYTE_GET(request->data[0], 2) & 0x7F; /* Obtains the first 7 bits */
	uint8_t num_write_bytes = BYTE_GET(request->data[0], 3);
	uint8_t num_read_bytes = BYTE_GET(request->data[1], 0);

	size_t remaining_write_size = sizeof(request->data) - 2 * sizeof(request->data[0]);
	size_t remaining_read_size = sizeof(request->data) - 1 * sizeof(request->data[0]);

	if (num_write_bytes > remaining_write_size || num_read_bytes > remaining_read_size) {
		return DATA_TOO_LARGE;
	}

	uint8_t *write_data_ptr = (uint8_t *)&request->data[2];
	uint8_t *read_data_ptr = (uint8_t *)&response->data[1];

	I2CInit(I2CMst, I2C_slave_address, I2CStandardMode, I2C_mst_id);
	uint32_t status = I2CTransaction(I2C_mst_id, write_data_ptr, num_write_bytes, read_data_ptr,
					 num_read_bytes);

	return status != 0;
}

REGISTER_MESSAGE(MSG_TYPE_I2C_MESSAGE, i2c_message_handler);
