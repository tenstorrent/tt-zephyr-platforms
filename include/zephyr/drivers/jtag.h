/*
 * Copyright (c) 2019 PHYTEC Messtechnik GmbH
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_ZEPHYR_DRIVERS_JTAG_H_
#define INCLUDE_ZEPHYR_DRIVERS_JTAG_H_

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_JTAG_EMUL
int jtag_emul_setup(const struct device *dev, uint32_t *buf, size_t buf_len);
int jtag_emul_axi_read32(const struct device *dev, uint32_t addr, uint32_t *value);
#endif

typedef int (*jtag_setup_api_t)(const struct device *dev);
typedef int (*jtag_teardown_api_t)(const struct device *dev);

typedef int (*jtag_tick_api_t)(const struct device *dev, uint32_t count);
typedef int (*jtag_reset_api_t)(const struct device *dev);
typedef int (*jtag_read_id_api_t)(const struct device *dev, uint32_t *id);

typedef int (*jtag_update_ir_api_t)(const struct device *dev, uint32_t count, const uint8_t *data);
typedef int (*jtag_update_dr_api_t)(const struct device *dev, bool idle, uint32_t count,
				    const uint8_t *data_in, uint8_t *data_out);

typedef int (*jtag_axi_read32_api_t)(const struct device *dev, uint32_t addr, uint32_t *value);
typedef int (*jtag_axi_write32_api_t)(const struct device *dev, uint32_t addr, uint32_t value);
typedef int (*jtag_axi_block_write_api_t)(const struct device *dev, uint32_t addr,
					  const uint32_t *value, uint32_t len);

struct jtag_api {
	jtag_setup_api_t setup;
	jtag_teardown_api_t teardown;

	jtag_tick_api_t tick;
	jtag_reset_api_t reset;
	jtag_read_id_api_t read_id;

	jtag_update_ir_api_t update_ir;
	jtag_update_dr_api_t update_dr;

	jtag_axi_read32_api_t axi_read32;
	jtag_axi_write32_api_t axi_write32;
	jtag_axi_block_write_api_t axi_block_write;
};

static inline int jtag_tick(const struct device *dev, uint32_t count)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->tick(dev, count);
}

static inline int jtag_read_id(const struct device *dev, uint32_t *id)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL || id == NULL) {
		return -EINVAL;
	}

	return api->read_id(dev, id);
}

static inline int jtag_reset(const struct device *dev)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->reset(dev);
}

static ALWAYS_INLINE int jtag_update_ir(const struct device *dev, uint32_t count,
					const uint8_t *data)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL || (data == NULL && count > 0)) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	return api->update_ir(dev, count, data);
}

static ALWAYS_INLINE int jtag_update_dr(const struct device *dev, bool idle, uint32_t count,
					const uint8_t *data_in, uint8_t *data_out)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL || (data_in == NULL && count > 0)) {
		return -EINVAL;
	}

	if (count == 0) {
		return 0;
	}

	return api->update_dr(dev, idle, count, data_in, data_out);
}

static inline int jtag_setup(const struct device *dev)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->setup(dev);
}

static inline int jtag_teardown(const struct device *dev)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->teardown(dev);
}

static inline int jtag_axi_read32(const struct device *dev, uint32_t addr, uint32_t *value)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->axi_read32(dev, addr, value);
}

static inline int jtag_axi_write32(const struct device *dev, uint32_t addr, uint32_t value)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->axi_write32(dev, addr, value);
}

static inline int jtag_axi_block_write(const struct device *dev, uint32_t addr,
				       const uint32_t *value, uint32_t len)
{
	const struct jtag_api *api = dev->api;

	if (dev == NULL) {
		return -EINVAL;
	}

	return api->axi_block_write(dev, addr, value, len);
}

#ifdef __cplusplus
}
#endif

#endif /* INCLUDE_ZEPHYR_DRIVERS_JTAG_H_ */
