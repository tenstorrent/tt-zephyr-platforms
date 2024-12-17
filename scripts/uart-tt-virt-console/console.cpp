/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "console.hpp"
#include "../../include/tenstorrent/uart_tt_virt.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define TENSTORRENT_MAPPING_RESOURCE0_UC 1
#define TENSTORRENT_MAPPING_RESOURCE0_WC 2

// ASCII Start of Heading (SOH) byte (or Ctrl-A)
constexpr uint8_t SOH = 0x01;
constexpr uint8_t CTRL_A = SOH;
static const std::string tt_device("/dev/tenstorrent/0");

#define TENSTORRENT_IOCTL_MAGIC           0xFA
#define TENSTORRENT_IOCTL_GET_DEVICE_INFO _IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS  _IO(TENSTORRENT_IOCTL_MAGIC, 2)

static uint8_t *map_bar0(int fd, size_t size);

using namespace tt;

using queues = uart_tt_virt_desc;

static inline bool can_push(const volatile queues *q)
{
	std::atomic_thread_fence(std::memory_order_acquire);
	// No reads/writes in the current thread can be reordered before this load
	return uart_tt_virt_desc_buf_space(q->rx_buf_capacity, q->rx_head, q->rx_tail) > 0;
}

static inline bool can_pop(const volatile queues *q)
{
	std::atomic_thread_fence(std::memory_order_acquire);
	// No reads/writes in the current thread can be reordered before this load
	return uart_tt_virt_desc_buf_size(q->tx_head, q->tx_tail) > 0;
}

static inline void push_char(volatile queues *q, char c)
{
	volatile uint8_t *const rx_buf = &q->buf[q->tx_buf_capacity];

	while (!can_push(q))
		;
	rx_buf[q->rx_head % q->rx_buf_capacity] = c;
	std::atomic_thread_fence(std::memory_order_release);
	// No reads/writes in the current thread can be reordered after this store
	++q->rx_head;
}

static inline char pop_char(volatile queues *q)
{
	while (!can_pop(q))
		;
	char c = q->buf[q->tx_tail % q->tx_buf_capacity];
	std::atomic_thread_fence(std::memory_order_release);
	// No reads/writes in the current thread can be reordered after this store
	++q->tx_tail;
	return c;
}

bool running = true;

class TerminalRawMode
{
	struct termios orig_termios;

      public:
	TerminalRawMode()
	{
		// Get the current terminal settings
		tcgetattr(STDIN_FILENO, &orig_termios);
		struct termios raw = orig_termios;

		// Modify the terminal settings for raw mode
		raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
		raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
		raw.c_oflag &= ~(OPOST);
		raw.c_cflag |= (CS8);

		// Apply the raw mode settings
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	}

	~TerminalRawMode()
	{
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
	}
};

uint64_t find_uart(BlackholePciDevice &device)
{
	return (uint64_t)(device.get_bar0() + 0x000004A8);
}

int uart_loop()
{
	std::cout << "Attempting to open " << tt_device << std::endl;

	BlackholePciDevice device(tt_device);
	uint64_t uart_base = find_uart(device);
	std::cout << std::hex << uart_base << std::dec << std::endl;

	volatile queues *q = reinterpret_cast<volatile queues *>(uart_base);

	TerminalRawMode raw_mode;
	bool ctrl_a_pressed = false;

	while (running) {
		if (q->magic != UART_TT_VIRT_MAGIC) {
			return -EAGAIN;
		}

		// Check for input from the terminal
		fd_set rfds;
		struct timeval tv;
		FD_ZERO(&rfds);
		FD_SET(STDIN_FILENO, &rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 1;

		int retval = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
		if (retval > 0) {
			char input;
			if (read(STDIN_FILENO, &input, 1) > 0) {
				if (ctrl_a_pressed) {
					if (input == 'a') {
						continue;
					}
					if (input == 'x') {
						running = false;
						std::cout << std::endl << std::endl;
						break;
					}
					ctrl_a_pressed = false;
				} else if (input == CTRL_A) {
					ctrl_a_pressed = true;
				} else {
					push_char(q, input);
				}
			}
		}

		// Check for output from the device
		if (can_pop(q)) {
			char c = pop_char(q);
			std::cout << c << std::flush;
		}
	}

	return 0;
}

int main()
{
	std::cout << "Press Ctrl-A x to exit." << std::endl << std::endl;
	while (running) {
		try {
			int r = uart_loop();
			if (r == -EAGAIN) {
				std::cout << "Error (UART vanished) -- was the chip reset?  "
					     "Retrying..."
					  << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			} else {
				return r;
			}
		} catch (const std::exception &e) {
			std::cout << "Error (" << e.what()
				  << ") -- was the chip reset?  Retrying..." << std::endl;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}

	std::cout << "Exiting..." << std::endl;
	return 0;
}

static PciDeviceInfo get_device_info(int fd)
{
	tenstorrent_get_device_info info{};

	ioctl(fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info);

	uint16_t bus = info.out.bus_dev_fn >> 8;
	uint16_t dev = (info.out.bus_dev_fn >> 3) & 0x1F;
	uint16_t fn = info.out.bus_dev_fn & 0x07;

	return PciDeviceInfo{
		info.out.vendor_id, info.out.device_id, info.out.pci_domain, bus, dev, fn};
}

BlackholePciDevice::BlackholePciDevice(const std::string &path)
	: fd(open(path.c_str(), O_RDWR | O_CLOEXEC)), info(get_device_info(fd)),
	  bar0_size(1ULL << 29), // 512 MiB
	  bar0(map_bar0(fd, bar0_size))
{
}

BlackholePciDevice::~BlackholePciDevice()
{
	munmap(bar0, bar0_size);
	close(fd);
}

static tenstorrent_mapping get_mapping(int fd, int id)
{
	static const size_t NUM_MAPPINGS = 8; // TODO(jms) magic 8
	struct {
		tenstorrent_query_mappings query_mappings{};
		tenstorrent_mapping mapping_array[NUM_MAPPINGS];
	} mappings;

	mappings.query_mappings.in.output_mapping_count = NUM_MAPPINGS;

	ioctl(fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, &mappings.query_mappings);

	for (size_t i = 0; i < NUM_MAPPINGS; i++) {
		if (mappings.mapping_array[i].mapping_id == id) {
			return mappings.mapping_array[i];
		}
	}

	throw std::runtime_error("Unknown mapping");
}

static uint8_t *map_bar0(int fd, size_t size)
{
	auto wc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_WC);
	auto uc_resource = get_mapping(fd, TENSTORRENT_MAPPING_RESOURCE0_UC);

	// There exists a convention that BAR0 is divided into write-combined (lower) and uncached
	// (upper) mappings.
	auto wc_size = 188 << 21;
	auto uc_size = uc_resource.mapping_size - wc_size;
	auto wc_offset = 0;
	auto uc_offset = wc_size;

	uc_resource.mapping_base += wc_size;

	auto *bar0 = static_cast<uint8_t *>(
		mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));

	if (bar0 == MAP_FAILED) {
		throw std::runtime_error("Failed to map BAR0");
	}

	void *wc = mmap(bar0 + wc_offset, wc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
			fd, wc_resource.mapping_base);
	void *uc = mmap(bar0 + uc_offset, uc_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
			fd, uc_resource.mapping_base);

	if (wc == MAP_FAILED) {
		throw std::runtime_error("Failed to map wc");
	}

	if (uc == MAP_FAILED) {
		throw std::runtime_error("Failed to map uc");
	}

	return bar0;
}
