/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_UART_TT_VIRT_H_
#define TENSTORRENT_UART_TT_VIRT_H_

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef RESET_UNIT_SCRATCH_RAM_BASE_ADDR
#define RESET_UNIT_SCRATCH_RAM_BASE_ADDR 0x80030400
#endif

#ifndef RESET_UNIT_SCRATCH_RAM_REG_ADDR
#define RESET_UNIT_SCRATCH_RAM_REG_ADDR(n)                                                         \
	(RESET_UNIT_SCRATCH_RAM_BASE_ADDR + sizeof(uint32_t) * (n))
#endif

/** @brief Magic identifier for Tenstorrent virtual UART (hex-speak for "TTSeRial") */
#define UART_TT_VIRT_MAGIC 0x775e21a1

#ifndef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR RESET_UNIT_SCRATCH_RAM_REG_ADDR(42)
#endif

/**
 * @brief In-memory ring buffer descriptor for Tenstorrent virtual UART.
 *
 * This in-memory ring buffer descriptor describes two ring buffers in a contiguous section of
 * memory. Following the descriptor, there are `tx_buf_size` bytes of space for the transmit
 * buffer, followed by `rx_buf_size` bytes of space for the receive buffer.
 *
 * Since using array-indices results in an ambiguity between an empty and full buffer when the
 * `head` and `tail` array-indices are equal, the `tx_head`, `tx_tail`, `rx_head`, and `rx_tail`
 * variables are up-counters (which may wrap around the 2^32 limit). Therefore, the buffer is
 * empty when the `head` and `tail` counters are equal, and the `tail` counter should never
 * exceed `head + buf_size` bytes (for transmit or receive).
 *
 * Since this descriptor is intended to be shared between both a device and host over shared
 * memory, it is important to clarify that the transmit (tx) and receive (rx) directions are from
 * the perspective of the device.
 *
 * TODO: add a nice ascii-art diagram.
 */
struct tt_vuart {
	uint32_t
		magic; /**< Descriptor is initialized when `magic` equals @ref VIRTUAL_UART_MAGIC */
	uint32_t tx_cap;   /**< Transmit buffer capacity, in bytes */
	uint32_t rx_cap;   /**< Receive buffer capacity, in bytes */
	uint32_t tx_head;  /** Transmit head counter */
	uint32_t tx_tail;  /** Transmit tail counter */
	uint32_t tx_oflow; /** Number of transmit overflows (device to host) */
	uint32_t rx_head;  /** Receive head counter */
	uint32_t rx_tail;  /** Receive tail counter */
	uint8_t buf[];     /** Buffer area of `tx_buf_size` bytes followed by `rx_buf_size` bytes */
};

enum tt_vuart_role {
	TT_VUART_ROLE_DEVICE,
	TT_VUART_ROLE_HOST,
};

static inline uint32_t tt_vuart_buf_size(uint32_t head, uint32_t tail)
{
	return tail - head;
}

static inline uint32_t tt_vuart_buf_space(uint32_t head, uint32_t tail, uint32_t cap)
{
	return cap - (tt_vuart_buf_size(head, tail));
}

static inline bool tt_vuart_buf_empty(uint32_t head, uint32_t tail)
{
	return tt_vuart_buf_size(head, tail) == 0;
}

static inline int tt_vuart_poll_in(volatile struct tt_vuart *vuart, unsigned char *p_char,
				   enum tt_vuart_role role)
{
	uint32_t cap;
	uint32_t offs;
	uint32_t tail;
	uint32_t head;
	volatile atomic_uint *headp;

	do {
		if (role == TT_VUART_ROLE_DEVICE) {
			headp = (volatile atomic_uint *)&vuart->rx_head;
			head = vuart->rx_head;
			tail = vuart->rx_tail;
			cap = vuart->rx_cap;
			offs = vuart->tx_cap;
		} else if (role == TT_VUART_ROLE_HOST) {
			headp = (volatile atomic_uint *)&vuart->tx_head;
			head = vuart->tx_head;
			tail = vuart->tx_tail;
			cap = vuart->tx_cap;
			offs = 0;
		} else {
			/* assert? */
		}

		if (head == tail) {
			/* if up-counters are equal, buffer is empty */
			return -1 /* EOF */;
		}

		if (atomic_compare_exchange_strong(headp, &head, head + 1)) {
			*p_char = vuart->buf[offs + (head % cap)];
			return *p_char;
		}
	} while (true);

	/* code unreachable */
}

static inline void tt_vuart_poll_out(volatile struct tt_vuart *vuart, unsigned char out_char,
				     enum tt_vuart_role role)
{
	uint32_t head;
	uint32_t cap;
	uint32_t offs;
	uint32_t lim;
	uint32_t tail;
	volatile atomic_uint *tailp;

	do {
		if (role == TT_VUART_ROLE_DEVICE) {
			tailp = (volatile atomic_uint *)&vuart->tx_tail;
			tail = vuart->tx_tail;
			head = vuart->tx_head;
			cap = vuart->tx_cap;
			offs = 0;
		} else if (role == TT_VUART_ROLE_HOST) {
			tailp = (volatile atomic_uint *)&vuart->rx_tail;
			tail = vuart->rx_tail;
			head = vuart->rx_head;
			cap = vuart->rx_cap;
			offs = vuart->tx_cap;
		} else {
			/* assert? */
		}

		lim = head + cap;

		if ((role == TT_VUART_ROLE_DEVICE) && (tail == lim)) {
			++vuart->tx_oflow;
			break;
		}

		if (atomic_compare_exchange_strong(tailp, &tail, tail + 1)) {
			vuart->buf[offs + (tail % cap)] = out_char;
			break;
		}
	} while (true);
}

#ifdef __ZEPHYR__
#include <zephyr/device.h>

enum uart_tt_virt_event {
	UART_TT_VIRT_EVENT_TX_DATA_READY,
};

typedef void (*uart_tt_virt_event_callback_t)(const struct device *dev,
					      enum uart_tt_virt_event event, void *user_data);

volatile struct tt_vuart *uart_tt_virt_get(const struct device *dev);
int uart_tt_virt_event_callback_set(const struct device *dev, uart_tt_virt_event_callback_t cb,
				    void *user_data);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TENSTORRENT_UART_TT_VIRT_H_ */
