/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRIPTS_TOOLING_LOGGING_H_
#define SCRIPTS_TOOLING_LOGGING_H_

/* Should be defined and set by main program */
extern int verbose;

#define D(level, fmt, ...)                                                                         \
	if (verbose >= level) {                                                                    \
		printf("D: %s(): " fmt "\n", __func__, ##__VA_ARGS__);                             \
	}

#define E(fmt, ...) fprintf(stderr, "E: %s(): " fmt "\n", __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                                                \
	if (verbose >= 0) {                                                                        \
		printf(fmt "\n", ##__VA_ARGS__);                                                   \
	}

#endif /* SCRIPTS_TOOLING_LOGGING_H_ */
