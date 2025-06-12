/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef __ZEPHYR__
#include "attrs.x"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <termios.h>
#include <sys/time.h>
#include <unistd.h>

#include <tenstorrent/uart_tt_virt.h>

#include "arc_jtag.h"
#include "arc_tlb.h"

#ifndef UART_TT_VIRT_MAGIC
#define UART_TT_VIRT_MAGIC 0x775e21a1
#endif

#ifndef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR 0x800304a0
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define UART_TT_VIRT_INVALID_ADDR 0xDEADBEAF

#define STATUS_POST_CODE_REG_ADDR 0x80030060
#define POST_CODE_PREFIX          0xc0de

#define BH_SCRAPPY_PCI_DEVICE_ID  0xb140

#define MSEC_PER_SEC  1000UL
#define USEC_PER_MSEC 1000UL
#define USEC_PER_SEC  1000000UL

#define VUART_NOT_READY_SLEEP_US (1 * USEC_PER_SEC)

/* ASCII Start of Heading (SOH) byte (or Ctrl-A) */
#define SOH       0x01
#define CTRL_A    SOH
#define TT_DEVICE "/dev/tenstorrent/0"

struct console {
	bool stop;
	const char *dev_name;
	uint32_t addr;  /* vuart discovery address */
	uint32_t magic; /* vuart magic */
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

	uint64_t timeout_abs_ms;

	/* backup of original termios settings */
	struct termios term;
};

/* Definition of functions used to access memory */
struct mem_access_driver {
	int (*start)(void *init_data);
	int (*read)(uint32_t addr, uint8_t *buf, size_t len);
	int (*write)(uint32_t addr, const uint8_t *buf, size_t len);
	void (*stop)(void);
};

static struct console _cons;
static int verbose;
static bool use_jtag;

static struct jtag_init_data jtag_init_data;
static struct tlb_init_data tlb_init_data = {
	.dev_name = TT_DEVICE,
	.pci_device_id = BH_SCRAPPY_PCI_DEVICE_ID,
	.tlb_id = BH_2M_TLB_UC_DYNAMIC_START + 1,
};

#define D(level, fmt, ...)                                                      \
	if (verbose >= level) {                                                 \
		printf("D: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__);        \
	}

#define E(fmt, ...) fprintf(stderr, "E: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                             \
	if (verbose >= 0) {                                                     \
		printf(fmt "\r\n", ##__VA_ARGS__);                              \
	}

static void console_init(struct console *cons)
{
	*cons = (struct console){
		.addr = UART_TT_VIRT_DISCOVERY_ADDR,
		.magic = UART_TT_VIRT_MAGIC,
		.vuart_addr = UART_TT_VIRT_INVALID_ADDR,
	};
}

static void dump_vuart_desc(struct mem_access_driver *driver, const struct console *cons)
{
	if ((cons == NULL) || (cons->vuart_addr == UART_TT_VIRT_INVALID_ADDR)) {
		return;
	}

	struct tt_vuart vuart;

	if (driver->read(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to read vuart descriptor");
		return;
	}

	D(2,
	  "vuart@0x%08X:\n"
	  "  magic: %x\n"
	  "  rx_cap: %u\n"
	  "  rx_head: %u\n"
	  "  rx_tail: %u\n"
	  "  tx_cap: %u\n"
	  "  tx_head: %u\n"
	  "  tx_oflow: %u\n"
	  "  tx_tail: %u\n"
	  "  version: %08x\n",
	  cons->vuart_addr, vuart.magic, vuart.rx_cap, vuart.rx_head, vuart.rx_tail,
	  vuart.tx_cap, vuart.tx_head, vuart.tx_oflow, vuart.tx_tail, vuart.version);
}

static int check_post_code(struct mem_access_driver *driver)
{
	union {
		struct {
			uint16_t code: 14;
			uint8_t id: 2;
			uint16_t prefix;
		};
		uint32_t data;
	} val = {
		.data = -1,
	};

	_Static_assert(sizeof(val) == sizeof(uint32_t), "invalid size of val");

	if (driver->read(STATUS_POST_CODE_REG_ADDR, (uint8_t *)&val.data, sizeof(val.data)) < 0) {
		E("failed to read post code");
		return -EIO;
	}
	if (val.prefix != POST_CODE_PREFIX) {
		E("prefix 0x%04x does not match expected prefix 0x%04x", val.prefix,
		  POST_CODE_PREFIX);
		return -EINVAL;
	}

	D(2, "POST code: (%04x, %02x, %04x)", val.prefix, val.id, val.code);

	return 0;
}

static int find_vuart(struct mem_access_driver *driver, struct console *cons)
{
	struct tt_vuart vuart;

	if (cons->vuart_addr != UART_TT_VIRT_INVALID_ADDR) {
		return 0;
	}

	if (driver->read(cons->addr, (uint8_t *)&cons->vuart_addr,
	    sizeof(cons->vuart_addr)) < 0) {
		E("failed to read vuart descriptor");
		return -EIO;
	}
	if (driver->read(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to read vuart descriptor");
		return -EIO;
	}

	if (vuart.magic != cons->magic) {
		E("0x%08x does not match expected magic 0x%08x", vuart.magic,
			cons->magic);
			return -EIO;
	}
	D(1, "found vuart descriptor at 0x%08X", cons->vuart_addr);

	dump_vuart_desc(driver, cons);
	return 0;
}

static int termio_raw(struct console *cons)
{
	if (!isatty(STDIN_FILENO)) {
		D(2, "Not an interactive console")
		return 0;
	}

	const uint8_t buf[sizeof(struct termios)] = {0};

	if (memcmp(buf, &cons->term, sizeof(buf)) == 0) {
		if (tcgetattr(STDIN_FILENO, &cons->term) < 0) {
			E("tcgetattr: %s", strerror(errno));
			return -errno;
		}
	}

	struct termios raw = cons->term;

	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON | ICRNL);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);

	if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) < 0) {
		E("tcsetattr: %s", strerror(errno));
		return -errno;
	}

	return 0;
}

static void termio_cooked(struct console *cons)
{
	const uint8_t buf[sizeof(struct termios)] = {0};

	if (memcmp(buf, &cons->term, sizeof(buf)) == 0) {
		/* not an interactive console */
		return;
	}

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &cons->term) < 0) {
		E("tcgetattr: %s", strerror(errno));
		return;
	}

	cons->term = (struct termios){0};
}

static inline size_t vuart_space(struct mem_access_driver *driver, struct console *cons)
{
	struct tt_vuart vuart;

	if (driver->read(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to read vuart descriptor");
		return -EIO;
	}

	if (vuart.magic != cons->magic) {
		return 0;
	}

	return vuart.rx_cap - tt_vuart_buf_size(vuart.rx_head, vuart.rx_tail);
}

static inline void vuart_putc(struct mem_access_driver *driver, struct console *cons, int ch)
{
	struct tt_vuart vuart;

	if (driver->read(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to read vuart descriptor");
		return;
	}

	if (vuart.magic != cons->magic) {
		return;
	}

	if (tt_vuart_buf_space(vuart.rx_head, vuart.rx_tail, vuart.rx_cap) > 0) {
		++vuart.rx_tail;
	}

	uint32_t buf_addr = cons->vuart_addr + offsetof(struct tt_vuart, buf) +
		vuart.tx_cap + (vuart.rx_tail % vuart.rx_cap);
	if (driver->write(buf_addr, (uint8_t *)&ch, sizeof(ch)) < 0) {
		E("failed to write vuart buffer");
		return;
	}

	if (driver->write(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to write vuart descriptor");
		return;
	}
}

static inline int vuart_read(struct mem_access_driver *driver, struct console *cons,
			     uint8_t *buf, size_t len)
{
	struct tt_vuart vuart;
	size_t read_len;

	if (driver->read(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to read vuart descriptor");
		return EOF;
	}

	if (vuart.magic != cons->magic) {
		return EOF;
	}

	if (tt_vuart_buf_empty(vuart.tx_head, vuart.tx_tail)) {
		return EOF;
	}

	read_len = MIN(len, tt_vuart_buf_size(vuart.tx_head, vuart.tx_tail));

	uint32_t buf_addr = cons->vuart_addr + offsetof(struct tt_vuart, buf) +
		(vuart.tx_head % vuart.tx_cap);
	if (driver->read(buf_addr, buf, read_len) < 0) {
		E("failed to read vuart buffer");
		return EOF;
	}
	vuart.tx_head += read_len;
	if (driver->write(cons->vuart_addr, (uint8_t *)&vuart, sizeof(vuart)) < 0) {
		E("failed to write vuart descriptor");
		return EOF;
	}

	return read_len;
}


static int loop(struct mem_access_driver *driver)
{
	int ret;
	bool ctrl_a_pressed = false;

	ret = check_post_code(driver);
	if (ret < 0) {
		goto out;
	}

	I("Press Ctrl-a,x to quit");

	while (!_cons.stop) {
		if (_cons.timeout_abs_ms != 0) {
			struct timeval now;
			uint64_t now_ms;

			gettimeofday(&now, NULL);
			now_ms = now.tv_sec * MSEC_PER_SEC + now.tv_usec / USEC_PER_MSEC;

			if (now_ms >= _cons.timeout_abs_ms) {
				D(2, "timeout reached");
				break;
			}
		}

		if (find_vuart(driver, &_cons) < 0) {
			usleep(VUART_NOT_READY_SLEEP_US);
			continue;
		}

		if (termio_raw(&_cons) < 0) {
			break;
		}

		uint8_t rbuf[256];
		int ch;

		/* dump anything available from the console before sending anything */
		while ((ret = vuart_read(driver, &_cons, rbuf, sizeof(rbuf))) != EOF) {
			for (int i = 0; i < ret; ++i) {
				if (rbuf[i] == '\n') {
					putchar('\r');
				}
				(void)putchar(rbuf[i]);
			}
		}

		fd_set fds;
		struct timeval tv = {
			.tv_usec = 1,
		};

		FD_ZERO(&fds);
		FD_SET(STDIN_FILENO, &fds);

		ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
		if (ret < 0) {
			E("select: %s", strerror(errno));
			break;
		}
		if (ret == 0) {
			continue;
		}

		ch = getchar();
		if (ctrl_a_pressed) {
			if (ch == 'x') {
				D(2, "Received Ctrl-a,x");
				_cons.stop = true;
				break;
			}
			/* assumes we only ever need to capture Ctrl-a,x */
			ctrl_a_pressed = false;
		} else {
			if (ch == CTRL_A) {
				ctrl_a_pressed = true;
				D(2, "Received Ctrl-a");
			} else {
				if (vuart_space(driver, &_cons) > 0) {
					vuart_putc(driver, &_cons, ch);
				} else {
					E("vuart buffer full");
				}
			}
		}
	}

out:
	driver->stop();
	termio_cooked(&_cons);
	return ret;
}

static void usage(const char *progname)
{
	I("Firmware console application for use with Tenstorrent PCIe cards\n"
	  "Copyright (c) 2025 Tenstorrent AI ULC\n"
	  "\n"
	  "\n"
	  "%s: %s [args..]\n"
	  "\n"
	  "args:\n"
	  "-a <addr>          : vuart discovery address (default: %08x)\n"
	  "-d <path>          : path to device node (default: %s)\n"
	  "-h                 : print this help message\n"
	  "-i <pci_device_id> : pci device id (default: %04x)\n"
	  "-j                 : Use JLink to connect to the device\n"
	  "-m <magic>         : vuart magic (default: %08x)\n"
	  "-q                 : decrease debug verbosity\n"
	  "-s <serial>        : Serial number of JLink device\n"
	  "-t <tlb_id>        : 2MiB TLB index (default: %u)\n"
	  "-v                 : increase debug verbosity\n"
	  "-w <timeout>       : wait timeout ms and exit\n",
	  __func__, progname, UART_TT_VIRT_DISCOVERY_ADDR, TT_DEVICE, BH_SCRAPPY_PCI_DEVICE_ID,
	  UART_TT_VIRT_MAGIC, BH_2M_TLB_UC_DYNAMIC_START + 1);
}

static int parse_args(struct console *cons, int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":a:d:hi:jm:qs:t:vw:")) != -1) {
		switch (c) {
		case 'a': {
			unsigned long addr;

			errno = 0;
			addr = strtol(optarg, NULL, 0);
			if (errno != 0) {
				E("invalid operand to -i %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}
			cons->addr = addr;
		} break;
		case 'd':
			tlb_init_data.dev_name = optarg;
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
		case 'i': {
			long pci_device_id;

			errno = 0;
			pci_device_id = strtol(optarg, NULL, 0);
			if ((pci_device_id < 0) || (pci_device_id > UINT16_MAX)) {
				errno = ERANGE;
			}
			if (errno != 0) {
				E("invalid operand to -i %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}
			tlb_init_data.pci_device_id = (uint16_t)pci_device_id;
		} break;
		case 'j': {
			use_jtag = true;
		} break;
		case 'm': {
			unsigned long magic;

			errno = 0;
			magic = strtol(optarg, NULL, 0);
			magic &= 0xffffffff;
			if (errno != 0) {
				E("invalid operand to -i %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}
			cons->magic = magic;
		} break;
		case 'q':
			--verbose;
			break;
		case 's':
			jtag_init_data.serial_number = optarg;
			break;
		case 't': {
			long tlb_id;

			errno = 0;
			tlb_id = strtol(optarg, NULL, 0);
			if ((tlb_id < BH_2M_TLB_UC_DYNAMIC_START) ||
			    (tlb_id > BH_2M_TLB_UC_DYNAMIC_END)) {
				errno = ERANGE;
			}
			if (errno != 0) {
				E("invalid operand to -i %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}
			tlb_init_data.tlb_id = (uint8_t)tlb_id;
		} break;
		case 'v':
			++verbose;
			break;
		case 'w': {
			long timeout;

			errno = 0;
			timeout = strtol(optarg, NULL, 0);
			if (timeout < 0) {
				errno = ERANGE;
			} else if (timeout == 0) {
				break;
			}
			if (errno != 0) {
				E("invalid operand to -i %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}

			struct timeval now;

			gettimeofday(&now, NULL);
			cons->timeout_abs_ms = (uint64_t)timeout + now.tv_sec * MSEC_PER_SEC +
					       now.tv_usec / USEC_PER_MSEC;
		} break;
		case ':':
			E("option -%c requires an operand\n", optopt);
			usage(basename(argv[0]));
			return -EINVAL;
		case '?':
			E("unrecognized option -%c\n", optopt);
			usage(basename(argv[0]));
			return -EINVAL;
		}
	}

	/* perform extra checking here and error as needed */

	return 0;
}

static void handler(int sig)
{
	I("\nCaught signal %d (%s)", sig, strsignal(sig));
	_cons.stop = true;
}

struct mem_access_driver tlb_driver = {
	.start = tlb_init,
	.read = tlb_read,
	.write = tlb_write,
	.stop = tlb_exit,
};

struct mem_access_driver jtag_driver = {
	.start = arc_jtag_init,
	.read = arc_jtag_read_mem,
	.write = arc_jtag_write_mem,
	.stop = arc_jtag_exit,
};

int main(int argc, char **argv)
{
	struct mem_access_driver *driver;
	void *init_data;
	console_init(&_cons);

	if (parse_args(&_cons, argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	if (signal(SIGINT, handler) == SIG_ERR) {
		E("signal: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	if (use_jtag) {
		jtag_init_data.verbose = verbose;
		init_data = &jtag_init_data;
		driver = &jtag_driver;
	} else {
		tlb_init_data.verbose = verbose;
		init_data = &tlb_init_data;
		driver = &tlb_driver;
	}

	if (driver->start(init_data) < 0) {
		goto error;
	}

	if (loop(driver) < 0) {
		goto error;
	}

	return EXIT_SUCCESS;

error:
	driver->stop();
	return EXIT_FAILURE;
}
