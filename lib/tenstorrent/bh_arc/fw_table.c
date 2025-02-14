/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fw_table.h"

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>

static FwTable fw_table;

/* Loader function that deserializes the fw table bin from the SPI filesystem */
int load_fw_table(uint8_t *buffer_space, uint32_t buffer_size)
{
	static const char fwTableTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "cmfwcfg";
	size_t bin_size = 0;

	if (tt_boot_fs_get_file(&boot_fs_data, fwTableTag, buffer_space, buffer_size, &bin_size) !=
	    TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return -1;
	}
	/* Convert the binary data to a pb_istream_t that is expected by decode */
	pb_istream_t stream = pb_istream_from_buffer(buffer_space, bin_size);
	/* PB_DECODE_NULLTERMINATED: Expect the message to be terminated with zero tag */
	bool status = pb_decode_ex(&stream, &FwTable_msg, &fw_table, PB_DECODE_NULLTERMINATED);

	if (!status) {
		/* Clear the table since a failed decode can leave it in an inconsistent state */
		memset(&fw_table, 0, sizeof(fw_table));
		/* Return -1 that should propagate up the stack */
		return -1;
	}
	return 0;
}

/* Getter function that returns a const pointer to the fw table */
const FwTable *get_fw_table(void)
{
	return &fw_table;
}
