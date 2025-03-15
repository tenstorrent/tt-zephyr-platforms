/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_vuart

#include <errno.h>
#include <stdatomic.h>

#include <tenstorrent/uart_tt_virt.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(uart_tt_virt, CONFIG_UART_LOG_LEVEL);

struct uart_tt_virt_config {
	volatile struct tt_vuart *vuart;
	uint32_t magic;
	uint32_t rx_cap;
	uint32_t tx_cap;
	uint32_t version;
	uint8_t inst;
	bool loopback;
};

struct uart_tt_virt_data {
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	struct uart_config cfg;
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	struct k_spinlock rx_lock;
	struct k_spinlock tx_lock;
	struct k_spinlock err_lock;

	bool err_irq_en;
	bool rx_irq_en;
	bool tx_irq_en;
	uint32_t err_flags;
	struct k_work irq_work;
	const struct device *dev;

	uart_irq_callback_user_data_t irq_cb;
	void *irq_cb_udata;
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

static int uart_tt_virt_irq_is_pending(const struct device *dev);
static int uart_tt_virt_irq_rx_ready(const struct device *dev);
static int uart_tt_virt_irq_tx_ready(const struct device *dev);

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
static int uart_tt_virt_config_get(const struct device *dev, struct uart_config *cfg)
{
	struct uart_tt_virt_data *data = dev->data;

	memcpy(cfg, &data->cfg, sizeof(*cfg));
	return 0;
}

static int uart_tt_virt_configure(const struct device *dev, const struct uart_config *cfg)
{
	struct uart_tt_virt_data *data = dev->data;

	if (cfg == NULL) {
		return -EINVAL;
	}

	if (!((cfg->parity >= UART_CFG_PARITY_NONE) && (cfg->parity <= UART_CFG_PARITY_SPACE))) {
		return -EINVAL;
	}

	if (!((cfg->stop_bits >= UART_CFG_STOP_BITS_0_5) &&
	      (cfg->stop_bits <= UART_CFG_STOP_BITS_2))) {
		return -EINVAL;
	}

	if (!((cfg->data_bits >= UART_CFG_DATA_BITS_5) &&
	      (cfg->data_bits <= UART_CFG_DATA_BITS_8))) {
		return -EINVAL;
	}

	if (!((cfg->flow_ctrl >= UART_CFG_FLOW_CTRL_NONE) &&
	      (cfg->flow_ctrl <= UART_CFG_FLOW_CTRL_RTS_CTS))) {
		return -EINVAL;
	}

	memcpy(&data->cfg, cfg, sizeof(*cfg));
	return 0;
}
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

static int uart_tt_virt_err_check(const struct device *dev)
{
	struct uart_tt_virt_data *data = dev->data;

	return !!data->err_flags;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static int uart_tt_virt_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	volatile struct tt_vuart *vuart = config->vuart;

	__ASSERT_NO_MSG(size >= 0);

	K_SPINLOCK(&data->tx_lock) {
		size = MIN((int)tt_vuart_buf_space(vuart->tx_head, vuart->tx_tail, vuart->tx_cap),
			   size);

		for (int i = 0; i < size; ++i) {
			tt_vuart_poll_out(vuart, *tx_data++, TT_VUART_ROLE_DEVICE);
		}
	}

	if (config->loopback && size > 0) {
		K_SPINLOCK(&data->rx_lock) {
			int lim = MIN(size, (int)tt_vuart_buf_space(vuart->rx_head, vuart->rx_tail,
								    vuart->rx_cap));

			for (int i = 0; i < lim; ++i) {
				unsigned char ch = -1;

				(void)tt_vuart_poll_in(vuart, &ch, TT_VUART_ROLE_HOST);
				tt_vuart_poll_out(vuart, ch, TT_VUART_ROLE_HOST);
			}

			/* Note: irq_handler() picks up rx data */
		}
	}

	return size;
}

static int uart_tt_virt_fifo_read(const struct device *dev, uint8_t *rx_data, int size)
{
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	volatile struct tt_vuart *vuart = config->vuart;

	__ASSERT_NO_MSG(size >= 0);

	K_SPINLOCK(&data->rx_lock) {
		size = MIN(size, (int)tt_vuart_buf_cap(vuart->rx_head, vuart->rx_tail));

		for (int i = 0; i < size; ++i) {
			(void)tt_vuart_poll_in(vuart, rx_data++, TT_VUART_ROLE_DEVICE);
		}
	}

	return size;
}

static void uart_tt_virt_irq_callback_set(const struct device *dev,
					  uart_irq_callback_user_data_t cb, void *user_data)
{
	struct uart_tt_virt_data *const data = dev->data;

	data->irq_cb = cb;
	data->irq_cb_udata = user_data;
}

static void uart_tt_virt_irq_err_disable(const struct device *dev)
{
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->err_lock) {
		data->err_irq_en = false;
	}
}

static void uart_tt_virt_irq_err_enable(const struct device *dev)
{
	bool submit;
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->err_lock) {
		data->err_irq_en = true;
		submit = !!data->err_flags;
	}

	if (submit) {
		(void)k_work_submit(&data->irq_work);
	}
}

static void uart_tt_virt_irq_handler(struct k_work *work)
{
	struct uart_tt_virt_data *data = CONTAINER_OF(work, struct uart_tt_virt_data, irq_work);
	const struct device *dev = data->dev;
	uart_irq_callback_user_data_t cb = data->irq_cb;
	void *udata = data->irq_cb_udata;

	if (cb == NULL) {
		LOG_DBG("No IRQ callback configured for uart_tt_virt device %p", dev);
		return;
	}

	while (uart_tt_virt_irq_is_pending(dev)) {
		cb(dev, udata);
	}
}

static int uart_tt_virt_irq_is_pending(const struct device *dev)
{
	return uart_tt_virt_irq_tx_ready(dev) || uart_tt_virt_irq_rx_ready(dev);
}

static void uart_tt_virt_irq_rx_disable(const struct device *dev)
{
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->rx_lock) {
		data->rx_irq_en = false;
	}
}

static void uart_tt_virt_irq_rx_enable(const struct device *dev)
{
	bool submit;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	K_SPINLOCK(&data->rx_lock) {
		data->rx_irq_en = true;
		submit = !tt_vuart_buf_empty(vuart->rx_head, vuart->rx_tail);
	}

	if (submit) {
		(void)k_work_submit(&data->irq_work);
	}
}

static int uart_tt_virt_irq_rx_ready(const struct device *dev)
{
	int available = 0;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	K_SPINLOCK(&data->rx_lock) {
		if (!data->rx_irq_en) {
			K_SPINLOCK_BREAK;
		}

		available = !tt_vuart_buf_empty(vuart->rx_head, vuart->rx_tail);
	}

	return available;
}

static int uart_tt_virt_irq_tx_complete(const struct device *dev)
{
	bool tx_complete = false;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	K_SPINLOCK(&data->tx_lock) {
		tx_complete = tt_vuart_buf_empty(vuart->tx_head, vuart->tx_tail);
	}

	return tx_complete;
}

static void uart_tt_virt_irq_tx_disable(const struct device *dev)
{
	struct uart_tt_virt_data *const data = dev->data;

	K_SPINLOCK(&data->tx_lock) {
		data->tx_irq_en = false;
	}
}

static void uart_tt_virt_irq_tx_enable(const struct device *dev)
{
	bool submit;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	K_SPINLOCK(&data->tx_lock) {
		data->tx_irq_en = true;
		submit = tt_vuart_buf_space(vuart->tx_head, vuart->tx_tail, vuart->tx_cap) > 0;
	}

	if (submit) {
		(void)k_work_submit(&data->irq_work);
	}
}

static int uart_tt_virt_irq_tx_ready(const struct device *dev)
{
	int available = 0;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	K_SPINLOCK(&data->tx_lock) {
		if (!data->tx_irq_en) {
			K_SPINLOCK_BREAK;
		}

		available = tt_vuart_buf_space(vuart->tx_head, vuart->tx_tail, vuart->tx_cap);
	}

	return available;
}

static int uart_tt_virt_irq_update(const struct device *dev)
{
	return 1;
}
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

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

static DEVICE_API(uart, uart_tt_virt_api) = {
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.config_get = uart_tt_virt_config_get,
	.configure = uart_tt_virt_configure,
#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */
	.err_check = uart_tt_virt_err_check,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_tt_virt_fifo_fill,
	.fifo_read = uart_tt_virt_fifo_read,
	.irq_callback_set = uart_tt_virt_irq_callback_set,
	.irq_err_disable = uart_tt_virt_irq_err_disable,
	.irq_err_enable = uart_tt_virt_irq_err_enable,
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
	.poll_in = uart_tt_virt_poll_in,
	.poll_out = uart_tt_virt_poll_out,
};

volatile struct tt_vuart *uart_tt_virt_get(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;

	return config->vuart;
}

static int uart_tt_virt_init(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_data *const data = dev->data;
	volatile struct tt_vuart *vuart = config->vuart;

	data->dev = dev;
	(void)k_work_init(&data->irq_work, uart_tt_virt_irq_handler);

	*vuart = (struct tt_vuart){
		.magic = config->magic,
		.version = (config->inst << 24) | (config->version & GENMASK(0, 24)),
		.rx_cap = config->rx_cap,
		.tx_cap = config->tx_cap,
	};

	return 0;
}

#define UART_TT_VIRT_DESC_SIZE(_inst)                                                              \
	(DIV_ROUND_UP(sizeof(struct tt_vuart) + DT_INST_PROP(_inst, rx_cap) +                      \
			      DT_INST_PROP(_inst, tx_cap),                                         \
		      sizeof(uint32_t)))

#define DEFINE_UART_TT_VIRT(_inst)                                                                 \
	static uint32_t tt_vuart_##_inst[UART_TT_VIRT_DESC_SIZE(_inst)];                           \
	static const struct uart_tt_virt_config uart_tt_virt_config_##_inst = {                    \
		.vuart = (struct tt_vuart *)&tt_vuart_##_inst,                                     \
		.version = DT_INST_PROP(_inst, version),                                           \
		.magic = DT_INST_PROP(_inst, magic),                                               \
		.rx_cap = DT_INST_PROP(_inst, tx_cap),                                             \
		.tx_cap = DT_INST_PROP(_inst, rx_cap),                                             \
		.loopback = DT_INST_PROP(_inst, loopback),                                         \
		.inst = _inst,                                                                     \
	};                                                                                         \
	static struct uart_tt_virt_data uart_tt_virt_data_##_inst;                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, uart_tt_virt_init, PM_DEVICE_DT_INST_GET(_inst),              \
			      &uart_tt_virt_data_##_inst, &uart_tt_virt_config_##_inst,            \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_tt_virt_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UART_TT_VIRT)
