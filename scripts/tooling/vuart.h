/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SCRIPTS_TOOLING_VUART_H_
#define SCRIPTS_TOOLING_VUART_H_

#include <sys/mman.h>

#ifndef UART_TT_VIRT_MAGIC
#define UART_TT_VIRT_MAGIC 0x775e21a1
#endif

#ifndef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR 0x800304a0
#endif

#define VUART_DATA_INIT(_dev_name, _addr, _magic, _pci_device_id, _channel)                        \
	{.dev_name = _dev_name,                                                                    \
	 .addr = _addr,                                                                            \
	 .magic = _magic,                                                                          \
	 .pci_device_id = _pci_device_id,                                                          \
	 .channel = _channel,                                                                      \
	 .fd = -1,                                                                                 \
	 .tlb = MAP_FAILED}

struct vuart_data {
	const char *dev_name;
	int fd;
	uint32_t addr;  /* vuart discovery address */
	uint32_t magic; /* vuart magic */
	uint16_t pci_device_id;
	uint32_t tlb_id;
	volatile uint8_t *tlb; /* 2MiB tlb window */

	/*
	 * TODO: consider associating stream numbers with rings. In firmware, a mapped ring can
	 * easily have multiple streams and they don't need to be contiguous. Just use an array of
	 * struct tt_vuart*'s in (a maximum-sized) series of scratch memory locations.
	 * e.g.
	 * [STDIN_FILENO] = ring0,
	 * [STDOUT_FILENO] = ring0, ring0 shares a buffer between (card) input and output
	 * [STDERR_FILENO] = ring1, ring1 uses the buffer exclusively for (card) output
	 */
	uint32_t vuart_addr;
	uint32_t channel;
	volatile struct tt_vuart *vuart;
	/* we might not actually need these */
	uint64_t wc_mapping_base;
	uint64_t uc_mapping_base;
};

/**
 * Open VUART descriptor and map TLB.
 * This function should be called before any other VUART operations
 * @param data Pointer to the VUART data structure, should be initialized by caller.
 * @return 0 on success, negative error code on failure.
 */
int vuart_open(struct vuart_data *data);

/**
 * Close VUART descriptor and unmap TLB.
 * @param data Pointer to the VUART data structure
 */
void vuart_close(struct vuart_data *data);

/**
 * Start VUART communication.
 * @param data Pointer to the VUART data structure
 * @return 0 on success, negative error code on failure.
 */
int vuart_start(struct vuart_data *data);

/**
 * Write a character to the VUART.
 * @param data Pointer to the VUART data structure
 * @param ch Character to write
 */
void vuart_putc(struct vuart_data *data, int ch);

/**
 * Get a character from the VUART.
 * @param data Pointer to the VUART data structure
 * @return Character read from the VUART, or EOF if no character is available or an error occurs.
 */
int vuart_getc(struct vuart_data *data);

/**
 * Get the available space in the VUART receive buffer.
 * @param data Pointer to the VUART data structure
 * @return Number of bytes available in the receive buffer
 */
size_t vuart_space(struct vuart_data *data);

/**
 * Bulk read data from VUART.
 * @param data Pointer to the VUART data structure
 * @param buf Buffer to read data into
 * @param size Number of bytes to read
 * @return Number of bytes read. May be less than size
 * @return -EAGAIN if no data is available
 */
int vuart_read(struct vuart_data *data, uint8_t *buf, size_t size);

#endif /* SCRIPTS_TOOLING_VUART_H_ */
