/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CLOCK_WAVE_H
#define CLOCK_WAVE_H

/** Available clock schemes to switch to*/
enum tt_clk_scheme {
	/** Zero skeq clock scheme*/
	TT_CLK_SCHEME_ZERO_SKEW = 0,

	/** Clock wave clock scheme*/
	TT_CLK_SCHEME_CLOCK_WAVE = 1,
};

#endif
