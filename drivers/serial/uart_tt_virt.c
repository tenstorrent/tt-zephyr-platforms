/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_uart_virt

#include <errno.h>
#include <stdatomic.h>

#include <tenstorrent/uart_tt_virt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(uart_tt_virt, CONFIG_UART_LOG_LEVEL);

#ifdef CONFIG_BOARD_TT_BLACKHOLE_TT_BLACKHOLE_SMC
#include "status_reg.h"

#undef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR ((uintptr_t *)RESET_UNIT_SCRATCH_RAM_REG_ADDR(42))
#else
#undef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR (&uart_tt_virt_discovery)
static uintptr_t uart_tt_virt_discovery;
#endif

struct uart_tt_virt_config {
	volatile struct tt_vuart *vuart;
};

struct uart_tt_virt_data {
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	struct uart_config cfg;
#endif
};

static int uart_tt_virt_poll_in(const struct device *dev, unsigned char *p_char)
{
	const struct uart_tt_virt_config *config = dev->config;
	volatile struct tt_vuart *vuart = config->vuart;

	return tt_vuart_poll_in(vuart, p_char, TT_VUART_ROLE_DEVICE);
}

void uart_tt_virt_poll_out(const struct device *dev, unsigned char out_char)
{
	const struct uart_tt_virt_config *config = dev->config;
	volatile struct tt_vuart *const vuart = config->vuart;

	tt_vuart_poll_out(vuart, out_char, TT_VUART_ROLE_DEVICE);
}

static int uart_tt_virt_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int uart_tt_virt_configure(const struct device *dev, const struct uart_config *cfg)
{
	struct uart_tt_virt_data *data = dev->data;

	memcpy(&data->cfg, cfg, sizeof(*cfg));
	return 0;
}

static int uart_tt_virt_config_get(const struct device *dev, struct uart_config *cfg)
{
	struct uart_tt_virt_data *data = dev->data;

	memcpy(cfg, &data->cfg, sizeof(*cfg));
	return 0;
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

static DEVICE_API(uart, uart_tt_virt_api) = {
	.poll_in = uart_tt_virt_poll_in,
	.poll_out = uart_tt_virt_poll_out,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_tt_virt_configure,
	.config_get = uart_tt_virt_config_get,
#endif
	.err_check = uart_tt_virt_err_check,
};

volatile struct tt_vuart *uart_tt_virt_get(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;

	return config->vuart;
}

static int uart_tt_virt_init(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;
	volatile struct tt_vuart *vuart = config->vuart;

	vuart->magic = UART_TT_VIRT_MAGIC;
	*UART_TT_VIRT_DISCOVERY_ADDR = (uintptr_t)vuart;

	return 0;
}

#define UART_TT_VIRT_TX_BUF_SIZE(_inst) DT_INST_PROP(_inst, tx_buf_size)
#define UART_TT_VIRT_RX_BUF_SIZE(_inst) DT_INST_PROP(_inst, rx_buf_size)

#define UART_TT_VIRT_DESC_SIZE(_inst)                                                              \
	(DIV_ROUND_UP(sizeof(struct tt_vuart) + UART_TT_VIRT_TX_BUF_SIZE(_inst) +                  \
			      UART_TT_VIRT_RX_BUF_SIZE(_inst),                                     \
		      sizeof(uint32_t)))

#define DEFINE_UART_TT_VIRT(_inst)                                                                 \
	static uint32_t tt_vuart_##_inst[UART_TT_VIRT_DESC_SIZE(_inst)];                           \
	static const struct uart_tt_virt_config uart_tt_virt_config_##_inst = {                    \
		.vuart = (struct tt_vuart *)&tt_vuart_##_inst,                                     \
	};                                                                                         \
	static struct uart_tt_virt_data uart_tt_virt_data_##_inst;                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, uart_tt_virt_init, PM_DEVICE_DT_INST_GET(_inst),              \
			      &uart_tt_virt_data_##_inst, &uart_tt_virt_config_##_inst,            \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_tt_virt_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UART_TT_VIRT)
