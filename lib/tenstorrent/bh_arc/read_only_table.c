/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "read_only_table.h"

#include <pb_decode.h>
#include <tenstorrent/tt_boot_fs.h>

#define BOARDTYPE_ORION 0x37
#define BOARDTYPE_P100  0x36
#define BOARDTYPE_P100A 0x43
#define BOARDTYPE_P150A 0x40
#define BOARDTYPE_P150  0x41
#define BOARDTYPE_P150C 0x42
#define BOARDTYPE_P300  0x44
#define BOARDTYPE_P300A 0x45
#define BOARDTYPE_P300C 0x46
#define BOARDTYPE_UBB   0x47

static ReadOnly read_only_table;

/* Loader function that deserializes the fw table bin from the SPI filesystem */
int load_read_only_table(uint8_t *buffer_space, uint32_t buffer_size)
{
	size_t bin_size = 0;
	static const char readOnlyTag[TT_BOOT_FS_IMAGE_TAG_SIZE] = "boardcfg";

	if (tt_boot_fs_get_file(&boot_fs_data, readOnlyTag, buffer_space, buffer_size, &bin_size) !=
	    TT_BOOT_FS_OK) {
		/* Error */
		/* TODO: Handle more gracefully */
		return -1;
	}
	/* Convert the binary data to a pb_istream_t that is expected by decode */
	pb_istream_t stream = pb_istream_from_buffer(buffer_space, bin_size);
	/* PB_DECODE_NULLTERMINATED: Expect the message to be terminated with zero tag */
	bool status =
		pb_decode_ex(&stream, &ReadOnly_msg, &read_only_table, PB_DECODE_NULLTERMINATED);

	if (!status) {
		/* Clear the table since a failed decode can leave it in an inconsistent state */
		memset(&read_only_table, 0, sizeof(read_only_table));
		/* Return -1 that should propagate up the stack */
		return -1;
	}

	return 0;
}

/* Getter function that returns a const pointer to the fw table */
const ReadOnly *get_read_only_table(void)
{
	return &read_only_table;
}

/* Converts a board id extracted from board type and converts it to a PCB Type */
PcbType get_pcb_type(void)
{
	PcbType pcb_type;

	/* Extract board type from board_id */
	uint8_t board_type = (uint8_t)((get_read_only_table()->board_id >> 36) & 0xFF);

	/* Figure out PCB type from board type */
	switch (board_type) {
	case BOARDTYPE_ORION:
		pcb_type = PcbTypeOrion;
		break;
	case BOARDTYPE_P100:
		pcb_type = PcbTypeP100;
		break;
	/* Note: the P100A is a depopulated P150, so PcbType is actually P150 */
	/* eth will be all disabled as per P100 specs anyways */
	case BOARDTYPE_P100A:
	case BOARDTYPE_P150:
	case BOARDTYPE_P150A:
	case BOARDTYPE_P150C:
		pcb_type = PcbTypeP150;
		break;
	case BOARDTYPE_P300:
	case BOARDTYPE_P300A:
	case BOARDTYPE_P300C:
		pcb_type = PcbTypeP300;
		break;
	case BOARDTYPE_UBB:
		pcb_type = PcbTypeUBB;
		break;
	default:
		pcb_type = PcbTypeUnknown;
		break;
	}

	return pcb_type;
}

uint32_t get_asic_location(void)
{
	return get_read_only_table()->asic_location;
}
