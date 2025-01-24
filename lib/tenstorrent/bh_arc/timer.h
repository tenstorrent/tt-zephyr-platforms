/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

#define NS_PER_REFCLK 20
#define REFCLK_F_MHZ  50
#define WAIT_1US      50                /* For 50 MHZ REFCLK (20 ns period) */
#define WAIT_100NS    5                 /* For 50 MHZ REFCLK (20 ns period) */
#define WAIT_1MS      (1000 * WAIT_1US) /* For 50MHz REFCLK (20ns period) */
#define WAIT_20NS     1                 /* For 50 MHz REFCLK */

uint64_t TimerTimestamp(void); /* Get current reflck timestamp */
void Wait(uint32_t cycles);

static inline uint32_t TimerGetCyclesForNsTime(uint32_t ns)
{
	return (ns + NS_PER_REFCLK - 1) / NS_PER_REFCLK;
}

static inline void WaitNs(uint32_t ns)
{
	uint32_t cycles = TimerGetCyclesForNsTime(ns);

	Wait(cycles);
}

static inline void WaitUs(uint32_t us)
{
	uint32_t cycles = TimerGetCyclesForNsTime(us * 1000);

	Wait(cycles);
}

static inline void WaitMs(uint32_t ms)
{
	uint32_t cycles = TimerGetCyclesForNsTime(ms * 1000000);

	Wait(cycles);
}
#endif
