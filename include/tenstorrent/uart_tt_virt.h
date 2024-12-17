/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_UART_TT_VIRT_H_
#define TENSTORRENT_UART_TT_VIRT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Magic identifier for Tenstorrent virtual UART (hex-speak for "TTSeRial") */
#define UART_TT_VIRT_MAGIC      0x775e21a1
#define UART_VIRT_DISCOVER_OFFS 0x000004A8

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
struct uart_tt_virt_desc {
	uint32_t
		magic; /**< Descriptor is initialized when `magic` equals @ref VIRTUAL_UART_MAGIC */
	uint32_t tx_buf_capacity; /**< Transmit buffer capacity, in bytes */
	uint32_t rx_buf_capacity; /**< Receive buffer capacity, in bytes */
	uint32_t tx_head;         /** Transmit head counter */
	uint32_t tx_tail;         /** Transmit tail counter */
	uint32_t tx_oflow;        /** Number of transmit overflows (device to host) */
	uint32_t rx_head;         /** Receive head counter */
	uint32_t rx_tail;         /** Receive tail counter */
	uint8_t buf[]; /** Buffer area of `tx_buf_size` bytes followed by `rx_buf_size` bytes */
};

static inline uint32_t uart_tt_virt_desc_buf_size(uint32_t head, uint32_t tail)
{
	return (tail >= head) ? (tail - head) : (head - tail);
}

static inline bool uart_tt_virt_desc_buf_empty(uint32_t head, uint32_t tail)
{
	return uart_tt_virt_desc_buf_size(head, tail) == 0;
}

static inline uint32_t uart_tt_virt_desc_buf_space(uint32_t capacity, uint32_t head, uint32_t tail)
{
	return capacity - uart_tt_virt_desc_buf_size(head, tail);
}

#ifdef __cplusplus
}
#endif

#endif /* TENSTORRENT_UART_TT_VIRT_H_ */
