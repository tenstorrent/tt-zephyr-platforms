/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef BH_DMFW_APP_JTAG_UGLY_SRC_JTAG_PROFILE_FUNCTIONS_H_
#define BH_DMFW_APP_JTAG_UGLY_SRC_JTAG_PROFILE_FUNCTIONS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_JTAG_PROFILE_FUNCTIONS

#define CYCLES_ENTRY()                                                                             \
	static struct cycle_cnt __cnt;                                                             \
	uint32_t __cyc = 0;                                                                        \
	uint32_t __ops = 0;                                                                        \
                                                                                                   \
	if (!__cnt.func) {                                                                         \
		__cyc = k_cycle_get_32();                                                          \
		__ops = io_ops;                                                                    \
	}

#define CYCLES_EXIT()                                                                              \
	do {                                                                                       \
		if (!__cnt.func) {                                                                 \
			__cnt.func = __func__;                                                     \
			__cnt.cycles = k_cycle_get_32() - __cyc;                                   \
			__cnt.io_ops = io_ops - __ops;                                             \
			printk("%s(): %d: finished in %u ms (%u cycles), %u "                      \
			       "io_ops\n",                                                         \
			       __func__, __LINE__,                                                 \
			       __cnt.cycles / (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000),         \
			       __cnt.cycles, __cnt.io_ops);                                        \
		}                                                                                  \
	} while (0)

/*
 * In files where I/O ops are counted add the line below.
 * The reason for that is that I/O ops are counted globally.
 *
 * static uint32_t io_ops;
 */
#define IO_OPS_INC() io_ops++

#else /* CONFIG_JTAG_PROFILE_FUNCTIONS */

#define CYCLES_ENTRY()
#define CYCLES_EXIT()
#define IO_OPS_INC()

#endif /* CONFIG_JTAG_PROFILE_FUNCTIONS */

struct cycle_cnt {
	const char *func;
	uint32_t cycles;
	uint32_t io_ops;
};

#ifdef __cplusplus
}
#endif

#endif /* BH_DMFW_APP_JTAG_UGLY_SRC_JTAG_PROFILE_FUNCTIONS_H_ */
