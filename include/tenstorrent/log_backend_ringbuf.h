/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOG_BACKEND_RINGBUF_H_
#define LOG_BACKEND_RINGBUF_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Read data from the Ring buffer log backend.
 * @param data Pointer to buffer to fill with data
 * @param length Length of buffer
 * @return Number of bytes read, or negative error code
 */
int log_backend_ringbuf_get_data(uint8_t *data, size_t length);

/**
 * Clear the ring buffer log backend.
 * Resets the ring buffer to empty, so new log messages will be written
 */
void log_backend_ringbuf_clear(void);

/**
 * Get the number of bytes currently stored in the ring buffer log backend.
 *
 * @return Number of bytes currently stored in the ring buffer
 */
size_t log_backend_ringbuf_get_used(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_BACKEND_RINGBUF_H_ */
