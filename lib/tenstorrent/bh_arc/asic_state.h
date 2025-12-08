/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ASIC_STATE_H
#define ASIC_STATE_H

typedef enum {
	A0State = 0, /**< normal operation state */
	A3State = 3, /**< no I2C transactions, at safe voltage/frequency */
} AsicState;

void lock_down_for_reset(void);
void set_asic_state(AsicState state);
AsicState get_asic_state(void);

#endif
