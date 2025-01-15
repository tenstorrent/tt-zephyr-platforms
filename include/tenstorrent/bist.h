/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_BIST_H_
#define INCLUDE_TENSTORRENT_BIST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run a fast built-in self test (BIST) on startup.
 */
int tt_bist(void);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_TENSTORRENT_BIST_H_ */
