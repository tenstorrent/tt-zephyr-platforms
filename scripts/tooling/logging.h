/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRIPTS_TOOLING_LOGGING_H_
#define SCRIPTS_TOOLING_LOGGING_H_

#include <stdint.h>
#include <stdio.h>
#include <time.h>

/* Should be defined and set by main program */
extern int verbose;

#define _log(file, fmt, ...) fprintf(file, fmt "\n", ##__VA_ARGS__)

#define _log_rl(msec, file, fmt, ...)                                                              \
	{                                                                                          \
		static uint64_t last_log_ms;                                                       \
		struct timespec ts;                                                                \
		uint64_t now;                                                                      \
                                                                                                   \
		clock_gettime(CLOCK_REALTIME, &ts);                                                \
		now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;                                     \
		if ((now - last_log_ms) >= (uint64_t)(msec)) {                                     \
			_log(file, fmt "\n", ##__VA_ARGS__);                                       \
			last_log_ms = now;                                                         \
		}                                                                                  \
	}

#define D(level, fmt, ...)                                                                         \
	if (verbose >= (level)) {                                                                  \
		_log(stdout, "D: %s(): " fmt, __func__, ##__VA_ARGS__);                            \
	}

#define E(fmt, ...) _log(stderr, "E: %s(): " fmt, __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                                                \
	if (verbose >= 0) {                                                                        \
		_log(stdout, fmt, ##__VA_ARGS__);                                                  \
	}

#define D_RL(level, msec, fmt, ...)                                                                \
	if (verbose >= (level)) {                                                                  \
		_log_rl((msec), stdout, "D: %s(): " fmt, __func__, ##__VA_ARGS__);                 \
	}

#define E_RL(msec, fmt, ...) _log_rl((msec), stderr, "E: %s(): " fmt, __func__, ##__VA_ARGS__);

#define I_RL(msec, fmt, ...) _log_rl((msec), stdout, fmt, __func__, ##__VA_ARGS__);

#endif /* SCRIPTS_TOOLING_LOGGING_H_ */
