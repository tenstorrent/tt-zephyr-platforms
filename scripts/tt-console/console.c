/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Build with:
 * gcc -O0 -g -Wall -Wextra -Werror -std=gnu11 -I ../../include -o tt-console console.c
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

#ifndef UART_TT_VIRT_MAGIC
#define UART_TT_VIRT_MAGIC 0x775e21a1
#endif

#ifndef UART_TT_VIRT_DISCOVERY_ADDR
#define UART_TT_VIRT_DISCOVERY_ADDR 0x800304a0
#endif

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52
#define BH_SCRAPPY_PCI_DEVICE_ID  0xb140

#define MSEC_PER_SEC  1000UL
#define USEC_PER_MSEC 1000UL
#define USEC_PER_SEC  1000000UL

#define VUART_NOT_READY_SLEEP_US (1 * USEC_PER_SEC)

#define KB(n) (1024 * (n))
#define MB(n) (1024 * 1024 * (n))

#ifndef PAGE_SIZE
#define PAGE_SIZE KB(4)
#endif

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#ifndef BIT_MASK
#define BIT_MASK(n) (BIT(n) - 1)
#endif

#define ARC_X 8
#define ARC_Y 0

#define TLB_2M_REG_SIZE            (3 * sizeof(uint32_t))
#define TLB_2M_SHIFT               21
#define TLB_2M_WINDOW_SIZE         BIT(TLB_2M_SHIFT)
#define TLB_2M_WINDOW_MASK         BIT_MASK(TLB_2M_SHIFT)
#define BH_2M_TLB_UC_DYNAMIC_START 190
#define BH_2M_TLB_UC_DYNAMIC_END   199
#define BH_NUM_2M_TLBS             202
#define BH_NUM_4G_TLBS             8
#define BH_NUM_TLBS                (BH_NUM_2M_TLBS + BH_NUM_4G_TLBS)

#define ARC_CSM_TLB 179

#define TLB_REGS_LEN PAGE_SIZE

#define ARC_CSM_BASE    0x10000000
#define TLB_CONFIG_ADDR 0x1FC00000

/* ASCII Start of Heading (SOH) byte (or Ctrl-A) */
#define SOH       0x01
#define CTRL_A    SOH
#define TT_DEVICE "/dev/tenstorrent/0"

#define TENSTORRENT_IOCTL_MAGIC           0xFA
#define TENSTORRENT_IOCTL_GET_DEVICE_INFO _IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_GET_DRIVER_INFO _IO(TENSTORRENT_IOCTL_MAGIC, 5)
#define TENSTORRENT_IOCTL_ALLOCATE_TLB    _IO(TENSTORRENT_IOCTL_MAGIC, 11)
#define TENSTORRENT_IOCTL_FREE_TLB        _IO(TENSTORRENT_IOCTL_MAGIC, 12)
#define TENSTORRENT_IOCTL_CONFIGURE_TLB   _IO(TENSTORRENT_IOCTL_MAGIC, 13)

enum tlb_order {
	TLB_ORDER_RELAXED,        /* Unposted AXI Writes. Relaxed NOC ordering */
	TLB_ORDER_STRICT,         /* Unposted AXI Writes. Strict NOC ordering */
	TLB_ORDER_POSTED_RELAXED, /* Posted AXI Writes. Relaxed NOC ordering */
	TLB_ORDER_POSTED_STRICT,  /* Posted AXI Writes. Strict NOC ordering */
};

#define STATUS_POST_CODE_REG_ADDR 0x80030060
#define POST_CODE_PREFIX          0xc0de

struct tenstorrent_get_device_info_inp {
	uint32_t output_size_bytes;
};

struct tenstorrent_get_device_info_out {
	uint32_t output_size_bytes;
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;
	uint16_t bus_dev_fn;
	uint16_t max_dma_buf_size_log2;
	uint16_t pci_domain;
};

struct tenstorrent_get_device_info {
	struct tenstorrent_get_device_info_inp in;
	struct tenstorrent_get_device_info_out out;
};

struct tenstorrent_get_driver_info_inp {
	uint32_t output_size_bytes;
};

struct tenstorrent_get_driver_info_out {
	uint32_t output_size_bytes;
	/* IOCTL API version */
	uint32_t driver_version;
	uint8_t driver_version_major;
	uint8_t driver_version_minor;
	uint8_t driver_version_patch;
	uint8_t reserved0;
};

struct tenstorrent_get_driver_info {
	struct tenstorrent_get_driver_info_inp in;
	struct tenstorrent_get_driver_info_out out;
};

struct tenstorrent_allocate_tlb_inp {
	uint64_t size;
	uint64_t reserved;
};

struct tenstorrent_allocate_tlb_out {
	uint32_t id;
	uint32_t reserved0;
	uint64_t mmap_offset_uc;
	uint64_t mmap_offset_wc;
	uint64_t reserved1;
};

struct tenstorrent_allocate_tlb {
	struct tenstorrent_allocate_tlb_inp in;
	struct tenstorrent_allocate_tlb_out out;
};

struct tenstorrent_free_tlb_inp {
	uint32_t id;
};

struct tenstorrent_free_tlb_out {
};

struct tenstorrent_free_tlb {
	struct tenstorrent_free_tlb_inp in;
	struct tenstorrent_free_tlb_out out;
};

struct tenstorrent_noc_tlb_config {
	uint64_t addr;
	uint16_t x_end;
	uint16_t y_end;
	uint16_t x_start;
	uint16_t y_start;
	uint8_t noc;
	uint8_t mcast;
	uint8_t ordering;
	uint8_t linked;
	uint8_t static_vc;
	uint8_t reserved0[3];
	uint32_t reserved1[2];
};

struct tenstorrent_configure_tlb_inp {
	uint32_t id;
	struct tenstorrent_noc_tlb_config config;
};

struct tenstorrent_configure_tlb_out {
	uint64_t reserved;
};

struct tenstorrent_configure_tlb {
	struct tenstorrent_configure_tlb_inp in;
	struct tenstorrent_configure_tlb_out out;
};

struct console {
	bool stop;
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
	volatile struct tt_vuart *vuart;
	/* we might not actually need these */
	uint64_t wc_mapping_base;
	uint64_t uc_mapping_base;

	uint64_t timeout_abs_ms;

	/* backup of original termios settings */
	struct termios term;
};

static struct console _cons;
static int verbose;

#define D(level, fmt, ...)                                                                         \
	if (verbose >= level) {                                                                    \
		printf("D: %s(): " fmt "\n", __func__, ##__VA_ARGS__);                             \
	}

#define E(fmt, ...) fprintf(stderr, "E: %s(): " fmt "\n", __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                                                \
	if (verbose >= 0) {                                                                        \
		printf(fmt "\n", ##__VA_ARGS__);                                                   \
	}

static void console_init(struct console *cons)
{
	*cons = (struct console){
		.dev_name = TT_DEVICE,
		.fd = -1,
		.addr = UART_TT_VIRT_DISCOVERY_ADDR,
		.magic = UART_TT_VIRT_MAGIC,
		.pci_device_id = BH_SCRAPPY_PCI_DEVICE_ID,
		.tlb = MAP_FAILED,
	};
}

static int program_noc(const struct console *cons, uint32_t x, uint32_t y, enum tlb_order order,
		       uint64_t phys, uint64_t *adjust)
{
	struct tenstorrent_configure_tlb tlb = {.in.id = cons->tlb_id,
						.in.config = (struct tenstorrent_noc_tlb_config){
							.addr = phys & ~TLB_2M_WINDOW_MASK,
							.x_end = x,
							.y_end = y,
							.ordering = order,
						}};

	if (ioctl(cons->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_GET_DRIVER_INFO): %s", strerror(errno));
		return -errno;
	}

	*adjust = phys & (uint64_t)TLB_2M_WINDOW_MASK;

	/* There isn't a new API for getting the current TLB programming */
	/* D(2, "tlb[%u]: %s", cons->tlb_id, tlb2m2str(reg)); */
	D(2, "tlb[%u]: %lx", cons->tlb_id, phys & ~TLB_2M_WINDOW_MASK);

	return 0;
}

static int64_t arc_read32(const struct console *cons, uint32_t phys)
{
	uint64_t adjust;
	int ret = program_noc(cons, ARC_X, ARC_Y, TLB_ORDER_STRICT, phys, &adjust);

	if (ret) {
		E("failed to configure tlb to point to ARC addr %x", phys);
		return ret;
	}
	uint32_t *virt = (uint32_t *)(cons->tlb + adjust);

	D(2, "32-bit read from (%p,%p) %p (phys,virt)", (void *)(uintptr_t)phys, virt,
	  (void *)(uintptr_t)adjust);

	return *virt;
}

static void dump_vuart_desc(const struct console *cons)
{
	if ((cons == NULL) || (cons->vuart == NULL)) {
		return;
	}

	struct tt_vuart vuart = *cons->vuart;

	D(2,
	  "vuart@%p:\n"
	  "  magic: %x\n"
	  "  rx_cap: %u\n"
	  "  rx_head: %u\n"
	  "  rx_tail: %u\n"
	  "  tx_cap: %u\n"
	  "  tx_head: %u\n"
	  "  tx_oflow: %u\n"
	  "  tx_tail: %u\n"
	  "  version: %08x\n",
	  cons->vuart, vuart.magic, vuart.rx_cap, vuart.rx_head, vuart.rx_tail, vuart.tx_cap,
	  vuart.tx_head, vuart.tx_oflow, vuart.tx_tail, vuart.version);
}

static int open_tt_dev(struct console *cons)
{
	if (cons->fd >= 0) {
		/* already opened or not properly initialized */
		return 0;
	}

	cons->fd = open(cons->dev_name, O_RDWR);
	if (cons->fd < 0) {
		E("%s: %s", strerror(errno), cons->dev_name);
		return -errno;
	}

	D(1, "opened %s as fd %d", cons->dev_name, cons->fd);

	struct tenstorrent_get_device_info info = {
		.in.output_size_bytes = sizeof(struct tenstorrent_get_device_info_out),
	};

	if (ioctl(cons->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_GET_DEVICE_INFO): %s", strerror(errno));
		return -errno;
	}

	uint16_t vid = info.out.vendor_id;
	uint16_t did = info.out.device_id;
	uint8_t bus = info.out.bus_dev_fn >> 8;
	uint8_t dev = (info.out.bus_dev_fn >> 3) & 0x1f;
	uint8_t fun = info.out.bus_dev_fn & 0x07;

	D(1, "opened %04x:%04x %02x.%02x.%x", vid, did, bus, dev, fun);

	if (vid != TENSTORRENT_PCI_VENDOR_ID) {
		E("expected vendor id %04x (not %04x)", TENSTORRENT_PCI_VENDOR_ID, vid);
		return -ENODEV;
	}

	if (did != cons->pci_device_id) {
		E("expected device id %04x (not %04x)", cons->pci_device_id, did);
		return -ENODEV;
	}

	struct tenstorrent_get_driver_info driver_info = {
		.in.output_size_bytes = sizeof(struct tenstorrent_get_driver_info_out),
	};

	if (ioctl(cons->fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &driver_info) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_GET_DRIVER_INFO): %s", strerror(errno));
		return -errno;
	}

	if (driver_info.out.driver_version < 2) {
		E("Need tlb allocation API requires at least driver version 2; have driver "
		  "version %d",
		  driver_info.out.driver_version);
		return -EFAULT;
	}

	return 0;
}

static void close_tt_dev(struct console *cons)
{
	if (cons->fd == -1) {
		/* not yet opened or already closed */
		return;
	}

	if (close(cons->fd) < 0) {
		E("fd %d: %s", cons->fd, strerror(errno));
		return;
	}

	D(1, "closed fd %d", cons->fd);

	cons->fd = -1;
}

/*
 * Map the 2MiB TLB window. This can remain mapped for the duration of the
 * application. We simply change where the TLB window points by writing to the TLB config
 * register.
 */
static int map_tlb(struct console *cons)
{
	if (cons->tlb != MAP_FAILED) {
		/* already mapped? cons improperly initialized? */
		return 0;
	}

	struct tenstorrent_allocate_tlb tlb = {.in.size = TLB_2M_WINDOW_SIZE};

	if (ioctl(cons->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_ALLOCATE_TLB): %s", strerror(errno));
		return -errno;
	}

	cons->tlb = mmap(NULL, TLB_2M_WINDOW_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, cons->fd,
			 tlb.out.mmap_offset_uc);
	if (cons->tlb == MAP_FAILED) {
		E("%s", strerror(errno));
		return -errno;
	}

	cons->tlb_id = tlb.out.id;

	D(1, "mapped %zu@%08x to %zu@%p for 2MiB TLB window %d", (size_t)TLB_2M_WINDOW_SIZE,
	  (uint32_t)tlb.out.mmap_offset_uc, (size_t)TLB_2M_WINDOW_SIZE, cons->tlb, cons->tlb_id);

	return 0;
}

static int unmap_tlb(struct console *cons)
{
	if (cons->tlb == MAP_FAILED) {
		/* not currently mapped */
		return 0;
	}

	if (munmap((void *)cons->tlb, TLB_2M_WINDOW_SIZE) < 0) {
		E("%s", strerror(errno));
		return -EFAULT;
	}

	D(1, "unmapped %zu@%p", (size_t)TLB_2M_WINDOW_SIZE, cons->tlb);

	struct tenstorrent_free_tlb tlb = {.in.id = cons->tlb_id};

	if (ioctl(cons->fd, TENSTORRENT_IOCTL_FREE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_ALLOCATE_TLB): %s", strerror(errno));
		return -errno;
	}

	cons->tlb = MAP_FAILED;

	return 0;
}

static int check_post_code(const struct console *cons)
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

	int64_t ret = arc_read32(cons, STATUS_POST_CODE_REG_ADDR);

	if (ret < 0) {
		E("failed to configure tlb to point to ARC addr %d", STATUS_POST_CODE_REG_ADDR);
		return ret;
	}
	val.data = ret;
	if (val.prefix != POST_CODE_PREFIX) {
		E("prefix 0x%04x does not match expected prefix 0x%04x", val.prefix,
		  POST_CODE_PREFIX);
		return -EINVAL;
	}

	D(2, "POST code: (%04x, %02x, %04x)", val.prefix, val.id, val.code);

	return 0;
}

static int find_vuart(struct console *cons)
{
	volatile struct tt_vuart *vuart = cons->vuart;
	uint32_t vuart_magic = (vuart == NULL) ? 0 : vuart->magic;

	if (vuart_magic != cons->magic) {
		int64_t ret = arc_read32(cons, cons->addr);

		if (ret < 0) {
			return ret;
		}
		cons->vuart_addr = ret;
		D(2, "discovery address: 0x%08x", cons->vuart_addr);

		uint64_t adjust;

		ret = program_noc(cons, ARC_X, ARC_Y, TLB_ORDER_STRICT, cons->vuart_addr, &adjust);
		if (ret) {
			E("failed to program NOC to point to the virtual uart (%x)",
			  cons->vuart_addr);
			return ret;
		}
		cons->vuart = (volatile struct tt_vuart *)(cons->tlb + adjust);

		if (cons->vuart->magic != cons->magic) {
			E("0x%08x does not match expected magic 0x%08x", cons->vuart->magic,
			  cons->magic);
			return -EIO;
		}

		D(1, "found vuart descriptor at %p", cons->vuart);

		dump_vuart_desc(cons);
	}

	return 0;
}

static void lose_vuart(struct console *cons)
{
	if (cons->vuart == NULL) {
		return;
	}

	cons->vuart = NULL;
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

static inline size_t vuart_space(struct console *cons)
{
	volatile struct tt_vuart *const vuart = cons->vuart;

	if (vuart->magic != cons->magic) {
		return 0;
	}

	return tt_vuart_buf_size(vuart->rx_head, vuart->rx_tail);
}

static inline void vuart_putc(struct console *cons, int ch)
{
	volatile struct tt_vuart *const vuart = cons->vuart;

	if (vuart->magic != cons->magic) {
		return;
	}

	if (tt_vuart_buf_space(vuart->rx_head, vuart->rx_tail, vuart->rx_cap) > 0) {
		++vuart->rx_tail;
	}

	volatile char *const rx_buf = (volatile char *)&vuart->buf[vuart->tx_cap];

	rx_buf[vuart->rx_tail % vuart->rx_cap] = ch;
}

static inline int vuart_getc(struct console *cons)
{
	volatile struct tt_vuart *const vuart = cons->vuart;

	if (vuart->magic != cons->magic) {
		return EOF;
	}

	if (tt_vuart_buf_empty(vuart->tx_head, vuart->tx_tail)) {
		return EOF;
	}

	volatile char *const tx_buf = (volatile char *)&vuart->buf[0];
	int ch = tx_buf[vuart->tx_head % vuart->tx_cap];

	++vuart->tx_head;

	return ch;
}

static int loop(struct console *const cons)
{
	int ret;
	bool ctrl_a_pressed = false;

	ret = open_tt_dev(cons);
	if (ret < 0) {
		goto out;
	}

	ret = map_tlb(cons);
	if (ret < 0) {
		goto out;
	}

	ret = check_post_code(cons);
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

		if (find_vuart(cons) < 0) {
			usleep(VUART_NOT_READY_SLEEP_US);
			continue;
		}

		if (termio_raw(cons) < 0) {
			break;
		}

		int ch;

		/* dump anything available from the console before sending anything */
		while ((ch = vuart_getc(cons)) != EOF) {
			if (ch == '\n') {
				putchar('\r');
			}
			(void)putchar(ch);
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
				if (vuart_space(cons) > 0) {
					vuart_putc(cons, ch);
				} else {
					ungetc(ch, stdin);
				}
			}
		}
	}

out:
	termio_cooked(cons);
	lose_vuart(cons);
	unmap_tlb(cons);
	close_tt_dev(cons);

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
	  "-m <magic>         : vuart magic (default: %08x)\n"
	  "-q                 : decrease debug verbosity\n"
	  "-v                 : increase debug verbosity\n"
	  "-w <timeout>       : wait timeout ms and exit\n",
	  __func__, progname, UART_TT_VIRT_DISCOVERY_ADDR, TT_DEVICE, BH_SCRAPPY_PCI_DEVICE_ID,
	  UART_TT_VIRT_MAGIC);
}

static int parse_args(struct console *cons, int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, ":a:d:hi:m:qt:vw:")) != -1) {
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
			cons->dev_name = optarg;
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
			cons->pci_device_id = (uint16_t)pci_device_id;
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
