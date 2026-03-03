/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief OCCP I3C backend
 *
 * I3C transport implementation for OCCP
 */

#include <tenstorrent/occp.h>

static int occp_i3c_send(const struct occp_backend *backend, const uint8_t *data, size_t length)
{
	struct occp_backend_i3c *i3c_backend = (struct occp_backend_i3c *)backend;

	return i3c_write(i3c_backend->i3c_dev, data, length);
}

static int occp_i3c_receive(const struct occp_backend *backend, uint8_t *data, size_t length)
{
	struct occp_backend_i3c *i3c_backend = (struct occp_backend_i3c *)backend;
	int ret;
	k_timepoint_t timeout = sys_timepoint_calc(K_MSEC(CONFIG_OCCP_I3C_READ_TIMEOUT_MS));

	do {
		ret = i3c_read(i3c_backend->i3c_dev, data, length);
		if (sys_timepoint_expired(timeout)) {
			return -ETIMEDOUT;
		}
	} while (ret == -EIO);
	return ret;
}

/**
 * @brief Initialize I3C backend
 * @param backend Pointer to I3C backend structure
 * @param i3c_dev Pointer to I3C device descriptor
 * @return 0 on success, negative error code on failure
 */
int occp_backend_i3c_init(struct occp_backend_i3c *backend, struct i3c_device_desc *i3c_dev)
{
	if (!backend || !i3c_dev) {
		return -EINVAL;
	}

	backend->base.send = occp_i3c_send;
	backend->base.receive = occp_i3c_receive;
	backend->i3c_dev = i3c_dev;

	return 0;
}
