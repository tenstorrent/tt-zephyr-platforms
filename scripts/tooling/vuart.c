/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __ZEPHYR__
#include "attrs.x"
#endif

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <tenstorrent/uart_tt_virt.h>

#include "logging.h"
#include "vuart.h"

#define PCI_DEVICES_PATH "/sys/bus/pci/devices"

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

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
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

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52

/* ASCII Start of Heading (SOH) byte (or Ctrl-A) */

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

static int pcie_read_unsigned_long_from_file(char *path, size_t path_size, unsigned long *value)
{
	int fd;
	int ret;
	char buf[32];

	(void)path_size;

	/*
	 * easier to use open / read / strtoul in this case than fopen / fscanf because the
	 * former supports optional 0x prefix and automatically detects base.
	 */

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		D(1, "Failed to open %s: %s", path, strerror(errno));
		return 0;
	}

	ret = read(fd, buf, sizeof(buf));
	if (ret < 0) {
		E("Failed to read from %s: %s", path, strerror(errno));
		goto out;
	}

	buf[ret] = '\0';
	errno = 0;
	*value = strtoul(buf, NULL, 0);
	ret = -errno;

out:
	close(fd);

	return ret;
}

static int pcie_walk_sysfs(uint16_t match_vid, uint16_t match_pid,
			   int (*cb)(char *path, size_t path_size, void *data), void *data)
{
	int ret;
	DIR *dir;
	unsigned long pid;
	unsigned long vid;
	struct dirent *dent;
	static char path[PATH_MAX];
	unsigned int counter = 0;

	_Static_assert(PATH_MAX >= 1024, "PATH_MAX is too small");

	ret = MIN(sizeof(path) - 1, strlen(PCI_DEVICES_PATH));
	strncpy(path, PCI_DEVICES_PATH, (size_t)ret);
	path[ret] = '\0';

	dir = opendir(path);
	if (dir == NULL) {
		E("Failed to open %s: %s", PCI_DEVICES_PATH, strerror(errno));
		return -errno;
	}

	for (dent = readdir(dir); dent != NULL; dent = readdir(dir)) {

#ifndef DT_LNK
#define DT_LNK 10
#endif

		D(3, "Found %s/%s with d_type %d", PCI_DEVICES_PATH, dent->d_name, dent->d_type);

		if (dent->d_type == DT_LNK) {
			snprintf(path, sizeof(path), "%s/%s/vendor", PCI_DEVICES_PATH,
				 dent->d_name);
			ret = pcie_read_unsigned_long_from_file(path, sizeof(path), &vid);
			if (ret != 0) {
				continue;
			}
			snprintf(path, sizeof(path), "%s/%s/device", PCI_DEVICES_PATH,
				 dent->d_name);
			ret = pcie_read_unsigned_long_from_file(path, sizeof(path), &pid);
			if (ret != 0) {
				continue;
			}

			if (((vid == match_vid) || (match_vid == 0xffff)) &&
			    ((pid == match_pid) || (match_pid == 0xffff))) {

				D(1, "Found %s/%s with vendor id %04lx and product id %04lx",
				  PCI_DEVICES_PATH, dent->d_name, vid, pid);

				snprintf(path, sizeof(path), "%s/%s", PCI_DEVICES_PATH,
					 dent->d_name);

				ret = cb(path, sizeof(path), data);
				if (ret < 0) {
					goto out;
				}
				counter += ret;
			}
		}
	}

	ret = counter;

out:
	closedir(dir);

	return ret;
}

static int pcie_remove_cb(char *path, size_t path_size, void *data)
{
	int fd;
	int ret;

	(void)data;

	strncat(path, "/remove", path_size);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		E("Failed to open %s: %s", path, strerror(errno));
		return -errno;
	}

	if (write(fd, "1", 1) < 0) {
		E("Failed to write to %s: %s", path, strerror(errno));
		ret = -errno;
		goto out;
	}

	path[strlen(path) - 7] = '\0';
	D(1, "Removed PCIe device %s", path);

	ret = 1; /* success: count the number of devices removed */

out:
	close(fd);

	return ret;
}

int pcie_remove(void)
{
	return pcie_walk_sysfs(TENSTORRENT_PCI_VENDOR_ID, 0xffff, pcie_remove_cb, NULL);
}

static int pcie_count_cb(char *path, size_t path_size, void *data)
{
	(void)path;
	(void)path_size;
	(void)data;

	return 1;
}

int pcie_rescan(void)
{
	int fd;
	int ret;

	fd = open("/sys/bus/pci/rescan", O_WRONLY);
	if (fd < 0) {
		E("Failed to open %s: %s", "/sys/bus/pci/rescan", strerror(errno));
		return -errno;
	}

	if (write(fd, "1", 1) < 0) {
		E("Failed to write to %s: %s", "/sys/bus/pci/rescan", strerror(errno));
		close(fd);
		return -errno;
	}

	close(fd);

	ret = pcie_walk_sysfs(TENSTORRENT_PCI_VENDOR_ID, 0xffff, pcie_count_cb, NULL);

	if (ret >= 0) {
		D(1, "Found %d Tenstorrent PCIe devices", ret);
	}

	return ret;
}

static int program_noc(const struct vuart_data *data, uint32_t x, uint32_t y, enum tlb_order order,
		       uint64_t phys, uint64_t *adjust)
{
	struct tenstorrent_configure_tlb tlb = {.in.id = data->tlb_id,
						.in.config = (struct tenstorrent_noc_tlb_config){
							.addr = phys & ~TLB_2M_WINDOW_MASK,
							.x_end = x,
							.y_end = y,
							.ordering = order,
						}};

	if (ioctl(data->fd, TENSTORRENT_IOCTL_CONFIGURE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_GET_DRIVER_INFO): %s", strerror(errno));
		return -errno;
	}

	*adjust = phys & (uint64_t)TLB_2M_WINDOW_MASK;

	/* There isn't a new API for getting the current TLB programming */
	/* D(2, "tlb[%u]: %s", data->tlb_id, tlb2m2str(reg)); */
	D(2, "tlb[%u]: %llx", data->tlb_id, (long long)phys & ~TLB_2M_WINDOW_MASK);

	return 0;
}

static int64_t arc_read32(const struct vuart_data *data, uint32_t phys)
{
	int ret;
	uint32_t *virt;
	uint64_t adjust;

	ret = program_noc(data, ARC_X, ARC_Y, TLB_ORDER_STRICT, phys, &adjust);
	if (ret) {
		E("failed to configure tlb to point to ARC addr %x: %d", phys, ret);
		return ret;
	}

	virt = (uint32_t *)(data->tlb + adjust);

	D(2, "32-bit read from (%p,%p) %p (phys,virt)", (void *)(uintptr_t)phys, virt,
	  (void *)(uintptr_t)adjust);

	return *virt;
}

static void dump_vuart_desc(const struct vuart_data *data)
{
	if ((data == NULL) || (data->vuart == NULL)) {
		return;
	}

	struct tt_vuart vuart = *data->vuart;

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
	  data->vuart, vuart.magic, vuart.rx_cap, vuart.rx_head, vuart.rx_tail, vuart.tx_cap,
	  vuart.tx_head, vuart.tx_oflow, vuart.tx_tail, vuart.version);
}

static int open_tt_dev(struct vuart_data *vuart)
{
	if (vuart->fd >= 0) {
		/* already opened or not properly initialized */
		return 0;
	}

	vuart->fd = open(vuart->dev_name, O_RDWR);
	if (vuart->fd < 0) {
		if (errno == ENOENT) {
			D(1, "%s: %s", strerror(errno), vuart->dev_name);
		} else {
			E("%s: %s", strerror(errno), vuart->dev_name);
		}
		return -errno;
	}

	D(1, "opened %s as fd %d", vuart->dev_name, vuart->fd);

	struct tenstorrent_get_device_info info = {
		.in.output_size_bytes = sizeof(struct tenstorrent_get_device_info_out),
	};

	if (ioctl(vuart->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
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
		return -ENOENT;
	}

	if (did != vuart->pci_device_id) {
		E("expected device id %04x (not %04x)", vuart->pci_device_id, did);
		return -ENOENT;
	}

	struct tenstorrent_get_driver_info driver_info = {
		.in.output_size_bytes = sizeof(struct tenstorrent_get_driver_info_out),
	};

	if (ioctl(vuart->fd, TENSTORRENT_IOCTL_GET_DRIVER_INFO, &driver_info) < 0) {
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

static void close_tt_dev(struct vuart_data *vart)
{
	if (vart->fd == -1) {
		/* not yet opened or already closed */
		return;
	}

	if (close(vart->fd) < 0) {
		vart->fd = -1;
		E("fd %d: %s", vart->fd, strerror(errno));
		return;
	}

	D(1, "closed fd %d", vart->fd);
	vart->fd = -1;
}

/*
 * Map the 2MiB TLB window. This can remain mapped for the duration of the
 * application. We simply change where the TLB window points by writing to the TLB config
 * register.
 */
static int map_tlb(struct vuart_data *vuart)
{
	if (vuart->tlb != MAP_FAILED) {
		/* already mapped? vuart improperly initialized? */
		return 0;
	}

	struct tenstorrent_allocate_tlb tlb = {.in.size = TLB_2M_WINDOW_SIZE};

	if (ioctl(vuart->fd, TENSTORRENT_IOCTL_ALLOCATE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_ALLOCATE_TLB): %s", strerror(errno));
		return -errno;
	}

	vuart->tlb = mmap(NULL, TLB_2M_WINDOW_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, vuart->fd,
			  tlb.out.mmap_offset_uc);
	if (vuart->tlb == MAP_FAILED) {
		E("%s", strerror(errno));
		return -errno;
	}

	vuart->tlb_id = tlb.out.id;

	D(1, "mapped %zu@%08x to %zu@%p for 2MiB TLB window %d", (size_t)TLB_2M_WINDOW_SIZE,
	  (uint32_t)tlb.out.mmap_offset_uc, (size_t)TLB_2M_WINDOW_SIZE, vuart->tlb, vuart->tlb_id);

	return 0;
}

static int unmap_tlb(struct vuart_data *vuart)
{
	if (vuart->tlb == MAP_FAILED) {
		/* not currently mapped */
		return 0;
	}

	if (munmap((void *)vuart->tlb, TLB_2M_WINDOW_SIZE) < 0) {
		E("%s", strerror(errno));
		return -EFAULT;
	}

	D(1, "unmapped %zu@%p", (size_t)TLB_2M_WINDOW_SIZE, vuart->tlb);

	struct tenstorrent_free_tlb tlb = {.in.id = vuart->tlb_id};

	if (ioctl(vuart->fd, TENSTORRENT_IOCTL_FREE_TLB, &tlb) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_ALLOCATE_TLB): %s", strerror(errno));
		return -errno;
	}

	vuart->tlb = MAP_FAILED;

	return 0;
}

static int check_post_code(const struct vuart_data *vuart)
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

	int64_t ret = arc_read32(vuart, STATUS_POST_CODE_REG_ADDR);

	if (ret < 0) {
		E("failed to configure tlb to point to ARC addr %d", STATUS_POST_CODE_REG_ADDR);
		return ret;
	}
	val.data = ret;
	if (val.prefix != POST_CODE_PREFIX) {
		D(1, "prefix 0x%04x does not match expected prefix 0x%04x", val.prefix,
		  POST_CODE_PREFIX);
		return -ENOENT;
	}

	D(2, "POST code: (%04x, %02x, %04x)", val.prefix, val.id, val.code);

	return 0;
}

/**
 * Open VUART descriptor and map TLB.
 * This function should be called before any other VUART operations
 * @param data Pointer to the VUART data structure, should be initialized by caller.
 * @return 0 on success, negative error code on failure.
 */
int vuart_open(struct vuart_data *data)
{
	int ret;

	if (data == NULL) {
		return -EINVAL;
	}

	ret = open_tt_dev(data);
	if (ret < 0) {
		goto fail;
	}

	ret = map_tlb(data);
	if (ret < 0) {
		goto fail;
	}

	ret = check_post_code(data);
	if (ret < 0) {
		goto fail;
	}

	return 0;
/* Error handler */
fail:
	vuart_close(data);
	return ret;
}

/**
 * Close VUART descriptor and unmap TLB.
 * @param data Pointer to the VUART data structure
 */
void vuart_close(struct vuart_data *data)
{
	data->vuart = NULL;
	unmap_tlb(data);
	close_tt_dev(data);
}

/**
 * Start VUART communication.
 * @param data Pointer to the VUART data structure
 * @return 0 on success, negative error code on failure.
 */
int vuart_start(struct vuart_data *data)
{
	volatile struct tt_vuart *vuart = data->vuart;
	uint32_t vuart_magic = (vuart == NULL) ? 0 : vuart->magic;

	if (vuart_magic != data->magic) {
		int64_t ret = arc_read32(data, data->addr);

		if (ret < 0) {
			return ret;
		}
		data->vuart_addr = ret;
		D(2, "discovery address: 0x%08x", data->vuart_addr);

		uint64_t adjust;

		ret = program_noc(data, ARC_X, ARC_Y, TLB_ORDER_STRICT, data->vuart_addr, &adjust);
		if (ret) {
			E("failed to program NOC to point to the virtual uart (%x): %d",
			  data->vuart_addr, (int)ret);
			return ret;
		}
		data->vuart = (volatile struct tt_vuart *)(data->tlb + adjust);

		if (data->vuart->magic != data->magic) {
			D(1, "0x%08x does not match expected magic 0x%08x", data->vuart->magic,
			  data->magic);
			return -ENOENT;
		}

		D(1, "found vuart descriptor at %p", data->vuart);

		dump_vuart_desc(data);
	}

	return 0;
}

/**
 * Write a character to the VUART.
 * @param data Pointer to the VUART data structure
 * @param ch Character to write
 */
void vuart_putc(struct vuart_data *data, int ch)
{
	volatile struct tt_vuart *const vuart = data->vuart;

	if (vuart->magic != data->magic) {
		return;
	}

	volatile char *const rx_buf = (volatile char *)&vuart->buf[vuart->tx_cap];

	rx_buf[vuart->rx_tail % vuart->rx_cap] = ch;

	if (tt_vuart_buf_space(vuart->rx_head, vuart->rx_tail, vuart->rx_cap) > 0) {
		++vuart->rx_tail;
	}
}

/**
 * Get the available space in the VUART receive buffer.
 * @param data Pointer to the VUART data structure
 * @return Number of bytes available in the receive buffer
 */
size_t vuart_space(struct vuart_data *data)
{
	volatile struct tt_vuart *const vuart = data->vuart;

	if (vuart->magic != data->magic) {
		return 0;
	}

	return tt_vuart_buf_space(vuart->rx_head, vuart->rx_tail, vuart->rx_cap);
}

/**
 * Get a character from the VUART.
 * @param data Pointer to the VUART data structure
 * @return Character read from the VUART, or EOF if no character is available or an error occurs.
 */
int vuart_getc(struct vuart_data *data)
{
	volatile struct tt_vuart *const vuart = data->vuart;

	if (vuart->magic != data->magic) {
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

/**
 * Bulk read data from VUART.
 * @param data Pointer to the VUART data structure
 * @param buf Buffer to read data into
 * @param size Number of bytes to read
 * @return Number of bytes read. May be less than size
 * @return -EAGAIN if no data is available
 */
int vuart_read(struct vuart_data *data, uint8_t *buf, size_t size)
{
	volatile struct tt_vuart *const vuart = data->vuart;
	uint32_t offset;

	if (vuart->magic != data->magic) {
		return -EAGAIN;
	}

	if (tt_vuart_buf_empty(vuart->tx_head, vuart->tx_tail)) {
		return -EAGAIN;
	}

	if (vuart->tx_oflow) {
		E("TX overflow detected, resetting flag");
		vuart->tx_oflow = 0;
	}

	size = MIN(size, tt_vuart_buf_size(vuart->tx_head, vuart->tx_tail));
	/* Cap copy size to end of vuart buffer */
	offset = vuart->tx_head % vuart->tx_cap;
	size = MIN(size, vuart->tx_cap - offset);

	/*
	 * Memcpy doesn't work with volatile buffers. However, metal uses a non
	 * volatile buffer for TLB access, and this seems safe in testing.
	 */
	memcpy(buf, (uint8_t *)&vuart->buf[offset], size);

	vuart->tx_head += size;

	return (int)size;
}
