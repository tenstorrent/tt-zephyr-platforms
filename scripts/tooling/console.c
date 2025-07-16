/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
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
#include <sys/select.h>
#include <termios.h>
#include <sys/time.h>
#include <unistd.h>

#include "logging.h"
#include "vuart.h"

#ifndef UART_TT_VIRT_MAGIC
#define UART_TT_VIRT_MAGIC 0x775e21a1
#endif

#ifndef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR 0x800304a0
#endif

#ifndef UART_CHANNEL
#define UART_CHANNEL 0
#endif

#define BH_SCRAPPY_PCI_DEVICE_ID 0xb140

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
	uint64_t timeout_abs_ms;

	/* backup of original termios settings */
	struct termios term;
	struct vuart_data vuart;
};

static struct console _cons;
int verbose;

static void console_init(struct console *cons)
{
	*cons = (struct console){
		.vuart = VUART_DATA_INIT(TT_DEVICE, UART_TT_VIRT_DISCOVERY_ADDR, UART_TT_VIRT_MAGIC,
					 BH_SCRAPPY_PCI_DEVICE_ID, UART_CHANNEL),
	};
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

static int loop(struct console *const cons)
{
	int ret;
	bool ctrl_a_pressed = false;

	ret = vuart_open(&cons->vuart);
	if (ret < 0) {
		goto out;
	}

	I("Press Ctrl-a,x to quit");

	while (!cons->stop) {
		if (cons->timeout_abs_ms != 0) {
			struct timeval now;
			uint64_t now_ms;

			gettimeofday(&now, NULL);
			now_ms = now.tv_sec * MSEC_PER_SEC + now.tv_usec / USEC_PER_MSEC;

			if (now_ms >= cons->timeout_abs_ms) {
				D(2, "timeout reached");
				break;
			}
		}

		if (vuart_start(&cons->vuart) < 0) {
			usleep(VUART_NOT_READY_SLEEP_US);
			continue;
		}

		if (termio_raw(cons) < 0) {
			break;
		}

		int ch;

		/* dump anything available from the console before sending anything */
		while ((ch = vuart_getc(&cons->vuart)) != EOF) {
			if (ch == '\n') {
				putchar('\r');
			}
			(void)putchar(ch);
			/* Flush to STDOUT */
			(void)fflush(stdout);
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
				cons->stop = true;
				break;
			}
			/* assumes we only ever need to capture Ctrl-a,x */
			ctrl_a_pressed = false;
		} else {
			if (ch == CTRL_A) {
				ctrl_a_pressed = true;
				D(2, "Received Ctrl-a");
			} else {
				if (vuart_space(&cons->vuart) > 0) {
					vuart_putc(&cons->vuart, ch);
				} else {
					ungetc(ch, stdin);
				}
			}
		}
	}

out:
	termio_cooked(cons);
	vuart_close(&cons->vuart);

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
	  "-c <channel>       : channel number (default: %d)\n"
	  "-d <path>          : path to device node (default: %s)\n"
	  "-h                 : print this help message\n"
	  "-i <pci_device_id> : pci device id (default: %04x)\n"
	  "-m <magic>         : vuart magic (default: %08x)\n"
	  "-q                 : decrease debug verbosity\n"
	  "-v                 : increase debug verbosity\n"
	  "-w <timeout>       : wait timeout ms and exit\n",
	  __func__, progname, UART_TT_VIRT_DISCOVERY_ADDR, UART_CHANNEL, TT_DEVICE,
	  BH_SCRAPPY_PCI_DEVICE_ID, UART_TT_VIRT_MAGIC);
}

static int parse_args(struct console *cons, int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":a:c:d:hi:m:qt:vw:")) != -1) {
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
			cons->vuart.addr = addr;
		} break;
		case 'c': {
			unsigned long channel;

			errno = 0;
			channel = strtol(optarg, NULL, 0);
			/* Limit to 16 channels so we don't use too many scratch registers */
			if (channel > 15) {
				E("Only channels 0-15 are supported, not %s", optarg);
				usage(basename(argv[0]));
				return -EINVAL;
			} else if (errno != 0) {
				E("invalid operand to -c %s: %s", optarg, strerror(errno));
				usage(basename(argv[0]));
				return -errno;
			}
			cons->vuart.channel = channel;
		} break;
		case 'd':
			cons->vuart.dev_name = optarg;
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
			cons->vuart.pci_device_id = (uint16_t)pci_device_id;
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
			cons->vuart.magic = magic;
		} break;
		case 'q':
			--verbose;
			break;
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

	/* Offset address based on channel selection */
	cons->vuart.addr += cons->vuart.channel * sizeof(uint32_t);

	/* perform extra checking here and error as needed */

	return 0;
}

static void handler(int sig)
{
	I("\nCaught signal %d (%s)", sig, strsignal(sig));
	_cons.stop = true;
}

int main(int argc, char **argv)
{
	console_init(&_cons);

	if (parse_args(&_cons, argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	if (signal(SIGINT, handler) == SIG_ERR) {
		E("signal: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	if (loop(&_cons) < 0) {
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
