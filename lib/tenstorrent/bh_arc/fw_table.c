/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "fw_table.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>
#include <zephyr/sys/util.h>

static FwTable fw_table = {
#ifdef CONFIG_TT_FWTABLE_MOCK
#include "tenstorrent/fw_table_mock.inc"
#endif
};

/* Loader function that deserializes the fw table bin from the SPI filesystem */
int load_fw_table(uint8_t *buffer_space, uint32_t buffer_size)
{
	if (IS_ENABLED(CONFIG_TT_FWTABLE_MOCK)) {
		return 0;
	}

	static const char fwTableTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "cmfwcfg";
	size_t bin_size = 0;

	if (tt_boot_fs_get_file(&boot_fs_data, fwTableTag, buffer_space, buffer_size, &bin_size) !=
	    TT_BOOT_FS_OK) {
		return -EIO;
	}
	/* Convert the binary data to a pb_istream_t that is expected by decode */
	pb_istream_t stream = pb_istream_from_buffer(buffer_space, bin_size);
	bool status = pb_decode_ex(&stream, &FwTable_msg, &fw_table, PB_DECODE_NULLTERMINATED);

	if (!status) {
		/* Clear the table to avoid an inconsistent state */
		memset(&fw_table, 0, sizeof(fw_table));
		return -EIO;
	}

	return 0;
}

/* Getter function that returns a const pointer to the fw table */
const FwTable *get_fw_table(void)
{
	return &fw_table;
}
