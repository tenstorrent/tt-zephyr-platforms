/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_uart_virt

#include "status_reg.h"

#include <errno.h>

#include <tenstorrent/uart_tt_virt.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(uart_tt_virt, CONFIG_UART_LOG_LEVEL);

#define UART_TT_VIRT_DISCOVERY_ADDR RESET_UNIT_SCRATCH_RAM_REG_ADDR(42)

struct uart_tt_virt_config {
	struct uart_tt_virt_desc *desc;
	uint32_t tx_buf_capacity;
	uint32_t rx_buf_capacity;
};

struct uart_tt_virt_data {
	struct k_spinlock rx_lock;
	struct k_spinlock tx_lock;
};

static int uart_tt_virt_poll_in(const struct device *dev, unsigned char *p_char)
{
	int ret = 0;
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_desc *desc = config->desc;

	K_SPINLOCK(&data->rx_lock) {
		uint32_t head = sys_read32((mem_addr_t)&desc->rx_head);
		uint32_t tail = sys_read32((mem_addr_t)&desc->rx_tail);

		if (head == tail) {
			/* if up-counters are equal, buffer is empty */
			ret = -1;
			K_SPINLOCK_BREAK;
		}

		*p_char = sys_read8((mem_addr_t)(&desc->buf[config->tx_buf_capacity +
							    (head % config->rx_buf_capacity)]));
		sys_write32(head + 1, (mem_addr_t)&desc->rx_head);
	}

	return ret;
}

static void uart_tt_virt_poll_out(const struct device *dev, unsigned char out_char)
{
	struct uart_tt_virt_data *data = dev->data;
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_desc *const desc = config->desc;

	K_SPINLOCK(&data->tx_lock) {
		uint32_t head = sys_read32((mem_addr_t)&desc->tx_head);
		uint32_t tail = sys_read32((mem_addr_t)&desc->tx_tail) + 1;

		if (tail >= head + config->tx_buf_capacity) {
			/*
			 * Normally, for physical uarts, a full TX buffer is not an issue; if
			 * transmit is enabled, then it is usually only a few microseconds until
			 * space becomes available. However, with this virtual uart, we rely on a
			 * host-side process to empty the buffer, which could mean intolerably long
			 * delays.
			 *
			 * The behaviour in this case is controlled by
			 * CONFIG_UART_TT_VIRT_OFLOW_CHOICE.
			 */
			if (IS_ENABLED(CONFIG_UART_TT_VIRT_OFLOW_HEAD)) {
				/*
				 * Note: this is inherently racey since the host updates the head
				 * counter
				 */
				sys_write8(out_char,
					   (mem_addr_t)&desc->buf[head % config->tx_buf_capacity]);
			} else if (IS_ENABLED(CONFIG_UART_TT_VIRT_OFLOW_TAIL)) {
				sys_write8(out_char,
					   (mem_addr_t)&desc
						   ->buf[(tail - 1) % config->tx_buf_capacity]);
			} else if (IS_ENABLED(CONFIG_UART_TT_VIRT_OFLOW_DROP)) {
				/* do nothing - character is lost */
			}

			/* increment overflow counter */
			sys_write32(sys_read32((mem_addr_t)&desc->tx_oflow) + 1,
				    (mem_addr_t)&desc->tx_oflow);

			K_SPINLOCK_BREAK;
		}

		sys_write8(out_char, (mem_addr_t)&desc->buf[tail % config->tx_buf_capacity]);
		sys_write32(tail, (mem_addr_t)&desc->tx_tail);
	}
}

static int uart_tt_virt_err_check(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static const struct uart_driver_api uart_tt_virt_api = {
	.poll_in = uart_tt_virt_poll_in,
	.poll_out = uart_tt_virt_poll_out,
	.err_check = uart_tt_virt_err_check,
};

static int uart_tt_virt_init(const struct device *dev)
{
	const struct uart_tt_virt_config *config = dev->config;
	struct uart_tt_virt_desc *desc = config->desc;

	sys_write32(config->tx_buf_capacity, (mem_addr_t)&desc->tx_buf_capacity);
	sys_write32(config->rx_buf_capacity, (mem_addr_t)&desc->rx_buf_capacity);
	sys_write32(UART_TT_VIRT_MAGIC, (mem_addr_t)&desc->magic);

	sys_write32((uint32_t)&desc, (mem_addr_t)UART_TT_VIRT_DISCOVERY_ADDR);

	return 0;
}

#define UART_TT_VIRT_TX_BUF_SIZE(_inst) DT_INST_PROP(_inst, tx_buf_size)
#define UART_TT_VIRT_RX_BUF_SIZE(_inst) DT_INST_PROP(_inst, rx_buf_size)

#define UART_TT_VIRT_DESC_SIZE(_inst)                                                              \
	(DIV_ROUND_UP(sizeof(struct uart_tt_virt_desc) + UART_TT_VIRT_TX_BUF_SIZE(_inst) +         \
			      UART_TT_VIRT_RX_BUF_SIZE(_inst),                                     \
		      sizeof(uint32_t)))

#define DEFINE_UART_TT_VIRT(_inst)                                                                 \
	static uint32_t uart_tt_virt_desc_##_inst[UART_TT_VIRT_DESC_SIZE(_inst)];                  \
	static const struct uart_tt_virt_config uart_tt_virt_config_##_inst = {                    \
		.desc = (struct uart_tt_virt_desc *)&uart_tt_virt_desc_##_inst,                    \
		.tx_buf_capacity = UART_TT_VIRT_TX_BUF_SIZE(_inst),                                \
		.rx_buf_capacity = UART_TT_VIRT_RX_BUF_SIZE(_inst),                                \
	};                                                                                         \
	static struct uart_tt_virt_data uart_tt_virt_data_##_inst;                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(_inst, uart_tt_virt_init, PM_DEVICE_DT_INST_GET(_inst),              \
			      &uart_tt_virt_data_##_inst, &uart_tt_virt_config_##_inst,            \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_tt_virt_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UART_TT_VIRT)
