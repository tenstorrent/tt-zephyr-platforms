/*
 * Copyright (c) 2023 Synopsys, Inc. All rights reserved.
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#if defined(CONFIG_ARC)
#ifdef _ASMLANGUAGE
.macro soc_early_asm_init_percpu
	mov  r0, 1	/* disable LPB for HS4XD */
	sr   r0, [_ARC_V2_LPB_CTRL]
.endm
#endif /* _ASMLANGUAGE */
#endif
