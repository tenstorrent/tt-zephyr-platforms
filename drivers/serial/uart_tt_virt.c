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

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	struct k_spinlock rx_lock;
	struct k_spinlock tx_lock;

	bool rx_irq_en;
	bool tx_irq_en;
	struct k_work irq_work;

	uart_irq_callback_user_data_t irq_cb;
	void *irq_cb_udata;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
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

static int uart_tt_virt_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int uart_tt_virt_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
#if 0
	int ret;
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	uint32_t put_size = MIN(config->latch_buffer_size, size);

	K_SPINLOCK(&data->tx_lock) {
		ret = ring_buf_put(data->tx_rb, tx_data, put_size);
	}

	if (config->loopback) {
		uart_tt_virt_put_rx_data(dev, (uint8_t *)tx_data, put_size);
	}

	uart_tt_virt_tx_data_ready(dev);

	return ret;
#else
	return -ENOSYS;
#endif
}

static int uart_tt_virt_fifo_read(const struct device *dev, uint8_t *rx_data, int size)
{
#if 0
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	uint32_t bytes_to_read;

	K_SPINLOCK(&data->rx_lock) {
		bytes_to_read = MIN(config->latch_buffer_size, ring_buf_size_get(data->rx_rb));
		bytes_to_read = MIN(bytes_to_read, size);
		ring_buf_get(data->rx_rb, rx_data, bytes_to_read);
	}

	return bytes_to_read;
#else
	return -ENOSYS;
#endif
}

static int uart_tt_virt_irq_tx_ready(const struct device *dev)
{
#if 0
	int available = 0;
	struct uart_tt_virt_data *data = dev->data;

	K_SPINLOCK(&data->tx_lock) {
		if (!data->tx_irq_en) {
			K_SPINLOCK_BREAK;
		}

		available = ring_buf_space_get(data->tx_rb);
	}

	return available;
#else
	return -ENOSYS;
#endif
}

static int uart_tt_virt_irq_rx_ready(const struct device *dev)
{
#if 0
	bool ready = false;
	struct uart_tt_virt_data *data = dev->data;

	K_SPINLOCK(&data->rx_lock) {
		if (!data->rx_irq_en) {
			K_SPINLOCK_BREAK;
		}

		ready = !ring_buf_is_empty(data->rx_rb);
	}

	return ready;
#else
	return -ENOSYS;
#endif
}

static void uart_tt_virt_irq_handler(struct k_work *work)
{
#if 0
	struct uart_tt_virt_data *data = CONTAINER_OF(work, struct uart_tt_virt_data, irq_work);
	const struct device *dev = data->dev;
	uart_irq_callback_user_data_t cb = data->irq_cb;
	void *udata = data->irq_cb_udata;

	if (cb == NULL) {
		LOG_DBG("No IRQ callback configured for uart_tt_virt device %p", dev);
		return;
	}

	while (true) {
		bool have_work = false;

		K_SPINLOCK(&data->tx_lock) {
			if (!data->tx_irq_en) {
				K_SPINLOCK_BREAK;
			}

			have_work = have_work || ring_buf_space_get(data->tx_rb) > 0;
		}

		K_SPINLOCK(&data->rx_lock) {
			if (!data->rx_irq_en) {
				K_SPINLOCK_BREAK;
			}

			have_work = have_work || !ring_buf_is_empty(data->rx_rb);
		}

		if (!have_work) {
			break;
		}

		cb(dev, udata);
	}
#endif
}

static int uart_tt_virt_irq_is_pending(const struct device *dev)
{
#if 0
	return uart_tt_virt_irq_tx_ready(dev) || uart_tt_virt_irq_rx_ready(dev);
#else
	return -ENOSYS;
#endif
}

static void uart_tt_virt_irq_tx_enable(const struct device *dev)
{
#if 0
	bool submit_irq_work;
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->tx_lock) {
		data->tx_irq_en = true;
		submit_irq_work = ring_buf_space_get(data->tx_rb) > 0;
	}

	if (submit_irq_work) {
		(void)k_work_submit_to_queue(&uart_tt_virt_work_q, &data->irq_work);
	}
#endif
}

static void uart_tt_virt_irq_rx_enable(const struct device *dev)
{
#if 0
	bool submit_irq_work;
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->rx_lock) {
		data->rx_irq_en = true;
		submit_irq_work = !ring_buf_is_empty(data->rx_rb);
	}

	if (submit_irq_work) {
		(void)k_work_submit_to_queue(&uart_tt_virt_work_q, &data->irq_work);
	}
#endif
}

static void uart_tt_virt_irq_tx_disable(const struct device *dev)
{
#if 0
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->tx_lock) {
		data->tx_irq_en = false;
	}
#endif
}

static void uart_tt_virt_irq_rx_disable(const struct device *dev)
{
#if 0
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->rx_lock) {
		data->rx_irq_en = false;
	}
#endif
}

static int uart_tt_virt_irq_tx_complete(const struct device *dev)
{
#if 0
	bool tx_complete = false;
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->tx_lock) {
		tx_complete = ring_buf_is_empty(data->tx_rb);
	}

	return tx_complete;
#else
	return -ENOSYS;
#endif
}

static void uart_tt_virt_irq_callback_set(const struct device *dev,
					  uart_irq_callback_user_data_t cb, void *user_data)
{
	struct uart_tt_virt_data *const data = dev->data;

	data->irq_cb = cb;
	data->irq_cb_udata = user_data;
}

static void uart_tt_virt_irq_err_enable(const struct device *dev)
{
}

static void uart_tt_virt_irq_err_disable(const struct device *dev)
{
}

static int uart_tt_virt_irq_update(const struct device *dev)
{
	return 1;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static DEVICE_API(uart, uart_tt_virt_api) = {
	.poll_in = uart_tt_virt_poll_in,
	.poll_out = uart_tt_virt_poll_out,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_tt_virt_configure,
	.config_get = uart_tt_virt_config_get,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
	.err_check = uart_tt_virt_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_tt_virt_fifo_fill,
	.fifo_read = uart_tt_virt_fifo_read,
	.irq_callback_set = uart_tt_virt_irq_callback_set,
	.irq_err_enable = uart_tt_virt_irq_err_enable,
	.irq_err_disable = uart_tt_virt_irq_err_disable,
	.irq_is_pending = uart_tt_virt_irq_is_pending,
	.irq_rx_disable = uart_tt_virt_irq_rx_disable,
	.irq_rx_enable = uart_tt_virt_irq_rx_enable,
	.irq_rx_ready = uart_tt_virt_irq_rx_ready,
	.irq_tx_complete = uart_tt_virt_irq_tx_complete,
	.irq_tx_disable = uart_tt_virt_irq_tx_disable,
	.irq_tx_enable = uart_tt_virt_irq_tx_enable,
	.irq_tx_ready = uart_tt_virt_irq_tx_ready,
	.irq_update = uart_tt_virt_irq_update,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

volatile struct tt_vuart *uart_tt_virt_get(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;

	return config->vuart;
}

int uart_tt_virt_irq_callback_set(const struct device *dev, uart_tt_virt_irq_callback_t cb,
				  void *user_data)
{
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
