/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#include <stdint.h>

#define DT_DRV_COMPAT tenstorrent_stm32_flash_wrapper

struct flash_stm32_wrapper_config {
	const struct device *flash_dev;
	uint32_t page_size;
	struct flash_pages_layout layout;
};

#define FLASH_STM32_DEVICE(inst) DT_INST_PHANDLE(inst, flash_device)

static int flash_stm32_wrapper_erase(const struct device *dev, off_t offset, size_t len)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_erase(flash_dev, offset, len);
}

static int flash_stm32_wrapper_write(const struct device *dev, off_t offset, const void *data,
				     size_t len)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_write(flash_dev, offset, data, len);
}

static int flash_stm32_wrapper_read(const struct device *dev, off_t offset, void *data, size_t len)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_read(flash_dev, offset, data, len);
}

static const struct flash_parameters *flash_stm32_wrapper_get_parameters(const struct device *dev)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_get_parameters(flash_dev);
}

static int flash_stm32_wrapper_get_size(const struct device *dev, uint64_t *size)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_get_size(flash_dev, size);
}

#ifdef CONFIG_FLASH_PAGE_LAYOUT
static void flash_stm32_wrapper_page_layout(const struct device *dev,
					    const struct flash_pages_layout **layout,
					    size_t *layout_size)
{
	const struct flash_stm32_wrapper_config *config = dev->config;

	*layout = &config->layout;
	*layout_size = 1;
}
#endif

#ifdef CONFIG_FLASH_EX_OP_ENABLED
static int flash_stm32_wrapper_ex_op(const struct device *dev, enum flash_ex_op op,
				     const struct flash_ex_op_params *params)
{
	const struct flash_stm32_wrapper_config *config = dev->config;
	const struct device *flash_dev = config->flash_dev;

	return flash_ex_op(flash_dev, op, params);
}
#endif

static DEVICE_API(flash, drv_api) = {
	.erase = flash_stm32_wrapper_erase,
	.write = flash_stm32_wrapper_write,
	.read = flash_stm32_wrapper_read,
	.get_parameters = flash_stm32_wrapper_get_parameters,
	.get_size = flash_stm32_wrapper_get_size,
#ifdef CONFIG_FLASH_PAGE_LAYOUT
	.page_layout = flash_stm32_wrapper_page_layout,
#endif
#ifdef CONFIG_FLASH_EX_OP_ENABLED
	.ex_op = flash_stm32_wrapper_ex_op,
#endif
};

#define TT_STM32_FLASH_WRAPPER(inst)                                                               \
	const struct flash_stm32_wrapper_config dev##inst##_config = {                             \
		.flash_dev = DEVICE_DT_GET(FLASH_STM32_DEVICE(inst)),                              \
		.page_size = DT_INST_PROP(inst, page_size),                                        \
		.layout =                                                                          \
			{                                                                          \
				.pages_count =                                                     \
					DT_INST_REG_SIZE(inst) / DT_INST_PROP(inst, page_size),    \
				.pages_size = DT_INST_PROP(inst, page_size),                       \
			},                                                                         \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, &dev##inst##_config, POST_KERNEL,            \
			      CONFIG_FLASH_INIT_PRIORITY, &drv_api);

DT_INST_FOREACH_STATUS_OKAY(TT_STM32_FLASH_WRAPPER)
