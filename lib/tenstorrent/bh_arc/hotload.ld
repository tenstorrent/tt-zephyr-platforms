/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

SECTION_PROLOGUE(.hotload,,)
{
	_hotload_start = .;
	KEEP(*(.hotload.*))
	_hotload_end = .;
} > CSM_PERSISTENT AT > SRAM
_hotload_load_addr = LOADADDR(.hotload);
_hotload_size = _hotload_end - _hotload_start;
