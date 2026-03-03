/**
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TT_ZEPHYR_PLATFORMS_INCLUDE_TENSTORRENT_OCCP_H_
#define TT_ZEPHYR_PLATFORMS_INCLUDE_TENSTORRENT_OCCP_H_

#include <stdint.h>
#include <zephyr/drivers/i3c.h>

/**
 * @brief Open Chiplet Configuration Protocol (OCCP) definitions
 */

struct occp_backend {
	int (*send)(const struct occp_backend *backend, const uint8_t *data, size_t length);
	int (*receive)(const struct occp_backend *backend, uint8_t *data, size_t length);
};

struct occp_backend_i3c {
	struct occp_backend base;
	/* I3C-specific fields */
	struct i3c_device_desc *i3c_dev;
};

/**
 * @brief Initialize I3C backend
 * @param backend Pointer to I3C backend structure
 * @param i3c_dev Pointer to I3C device descriptor
 * @return 0 on success, negative error code on failure
 */
int occp_backend_i3c_init(struct occp_backend_i3c *backend, struct i3c_device_desc *i3c_dev);

/**
 * @brief Get OCCP protocol version
 * @param backend OCCP backend to use
 * @param major Pointer to store major version
 * @param minor Pointer to store minor version
 * @param patch Pointer to store patch version
 * @return 0 on success, negative error code on failure
 */
int occp_get_version(const struct occp_backend *backend, uint8_t *major, uint8_t *minor,
		     uint8_t *patch);

/**
 * @brief Write data to OCCP device
 * @param backend OCCP backend to use
 * @param address Address to write to
 * @param data Pointer to data to write
 * @param length Length of data to write
 * @return 0 on success, negative error code on failure
 */
int occp_write_data(const struct occp_backend *backend, uint64_t address, const uint8_t *data,
		    size_t length);

/**
 * @brief Read data from OCCP device
 * @param backend OCCP backend to use
 * @param address Address to read from
 * @param data Pointer to buffer to store read data
 * @param length Length of data to read
 * @return 0 on success, negative error code on failure
 */
int occp_read_data(const struct occp_backend *backend, uint64_t address, uint8_t *data,
		   size_t length);

/**
 * @brief Execute image at specified address
 * @param backend OCCP backend to use
 * @param execution_address Address to execute image from
 * @param cpu_id CPU ID to execute on
 * @return 0 on success, negative error code on failure
 */
int occp_execute_image(const struct occp_backend *backend, uint64_t execution_address,
		       uint8_t cpu_id);

#endif /* TT_ZEPHYR_PLATFORMS_INCLUDE_TENSTORRENT_OCCP_H_ */
