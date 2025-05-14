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

#include "arc_tlb.h"

#define ARC_X 8
#define ARC_Y 0

#define TLB_2M_REG_SIZE            (3 * sizeof(uint32_t))
#define TLB_2M_SHIFT               21
#define TLB_2M_WINDOW_SIZE         BIT(TLB_2M_SHIFT)
#define TLB_2M_WINDOW_MASK         BIT_MASK(TLB_2M_SHIFT)
#define BH_NUM_2M_TLBS             202
#define BH_NUM_4G_TLBS             8
#define BH_NUM_TLBS                (BH_NUM_2M_TLBS + BH_NUM_4G_TLBS)

#define ARC_CSM_TLB 179

#define TLB_REGS_LEN PAGE_SIZE

#define ARC_CSM_BASE    0x10000000
#define TLB_CONFIG_ADDR 0x1FC00000

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52
#define TENSTORRENT_IOCTL_MAGIC           0xFA
#define TENSTORRENT_IOCTL_GET_DEVICE_INFO _IO(TENSTORRENT_IOCTL_MAGIC, 0)
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS  _IO(TENSTORRENT_IOCTL_MAGIC, 2)

#define NUM_TENSTORRENT_QUERY_MAPPINGS   8
#define TENSTORRENT_MAPPING_RESOURCE0_UC 1
#define TENSTORRENT_MAPPING_RESOURCE0_WC 2

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

enum tlb_order {
	TLB_ORDER_RELAXED,        /* Unposted AXI Writes. Relaxed NOC ordering */
	TLB_ORDER_STRICT,         /* Unposted AXI Writes. Strict NOC ordering */
	TLB_ORDER_POSTED_RELAXED, /* Posted AXI Writes. Relaxed NOC ordering */
	TLB_ORDER_POSTED_STRICT,  /* Posted AXI Writes. Strict NOC ordering */
};

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

struct tenstorrent_mapping {
	uint32_t mapping_id;
	uint32_t: 32;
	uint64_t mapping_base;
	uint64_t mapping_size;
};

struct tenstorrent_query_mappings_inp {
	uint32_t output_mapping_count;
	uint32_t: 32;
};

struct tenstorrent_query_mappings_out {
	struct tenstorrent_mapping mappings[0];
};

struct tenstorrent_query_mappings {
	struct tenstorrent_query_mappings_inp in;
	struct tenstorrent_query_mappings_out out;
};

struct tlb_2m {
	uint64_t address: 43;
	uint64_t x_end: 6;
	uint64_t y_end: 6;
	uint64_t x_start: 6;
	uint64_t y_start: 6;
	uint64_t noc: 2;
	uint64_t multicast: 1;
	uint64_t ordering: 2;
	uint64_t linked: 1;
	uint64_t use_static_vc: 1;
	uint64_t stream_header: 1;
	uint64_t static_vc: 3;
	uint64_t: 18; /* Reserved - RAZ / WAZ */
} __packed __aligned(4);
_Static_assert(sizeof(struct tlb_2m) == TLB_2M_REG_SIZE, "incongruent struct tlb_2m size");

struct tlb_data {
	int fd;
	const char *dev_name;
	uint16_t pci_device_id;
	uint8_t tlb_id;
	volatile uint8_t *tlb;            /* 2MiB tlb window */
	volatile struct tlb_2m *tlb_regs; /* 4kiB tlb register space */
	uint64_t programmed_phys; /* last programmed physical address */
	/* we might not actually need these */
	uint64_t wc_mapping_base;
	uint64_t uc_mapping_base;
};

static struct tlb_data _tlb_data;
static int verbose;

#define D(level, fmt, ...)                                                      \
	if (verbose >= level) {                                                 \
		printf("D: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__);        \
	}

#define E(fmt, ...) fprintf(stderr, "E: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                             \
	if (verbose >= 0) {                                                     \
		printf(fmt "\r\n", ##__VA_ARGS__);                              \
	}

static const char *tlb2m2str(volatile struct tlb_2m *reg)
{
	static char buf[128] = {0};
	volatile uint32_t *data = (volatile uint32_t *)reg;

	snprintf(buf, sizeof(buf), "(0x%x, 0x%x, 0x%x)", data[0], data[1], data[2]);

	return buf;
}

static uint64_t program_noc(struct tlb_data *tlb_data, uint32_t x, uint32_t y,
			    enum tlb_order order, uint64_t phys)
{
	volatile struct tlb_2m *const reg = &tlb_data->tlb_regs[tlb_data->tlb_id];

	uint64_t addr = phys >> TLB_2M_SHIFT;

	if (addr != tlb_data->programmed_phys) {
		/* Reprogram */
		*reg = (struct tlb_2m){
			.address = addr,
			.x_end = x,
			.y_end = y,
			.ordering = order,
		};
		tlb_data->programmed_phys = addr;
	}

	uint64_t adjust = phys & TLB_2M_WINDOW_MASK;

	D(2, "tlb[%u]: %s", tlb_data->tlb_id, tlb2m2str(reg));

	return adjust;
}

static int open_tt_dev(struct tlb_data *data)
{

	struct tenstorrent_get_device_info info = {
		.in.output_size_bytes = sizeof(struct tenstorrent_get_device_info_out),
	};

	if (data->fd >= 0) {
		/* already opened or not properly initialized */
		return 0;
	}
	data->fd = open(data->dev_name, O_RDWR);
	if (data->fd < 0) {
		E("%s: %s", strerror(errno), data->dev_name);
		return -errno;
	}
	D(1, "opened %s as fd %d", data->dev_name, data->fd);


	if (ioctl(data->fd, TENSTORRENT_IOCTL_GET_DEVICE_INFO, &info) < 0) {
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

	if (did != data->pci_device_id) {
		E("expected device id %04x (not %04x)", data->pci_device_id, did);
		return -ENODEV;
	}

	uint64_t buf[(sizeof(struct tenstorrent_query_mappings) +
		      NUM_TENSTORRENT_QUERY_MAPPINGS * sizeof(struct tenstorrent_mapping)) /
		     sizeof(uint64_t)] = {0};

	struct tenstorrent_query_mappings *const mapping = (struct tenstorrent_query_mappings *)buf;

	mapping->in.output_mapping_count = NUM_TENSTORRENT_QUERY_MAPPINGS;

	struct tenstorrent_mapping *const omap = mapping->out.mappings;

	if (ioctl(data->fd, TENSTORRENT_IOCTL_QUERY_MAPPINGS, mapping) < 0) {
		E("ioctl(TENSTORRENT_IOCTL_QUERY_MAPPINGS): %s", strerror(errno));
		return -errno;
	}

	for (int i = 0; i < NUM_TENSTORRENT_QUERY_MAPPINGS; ++i) {
		const char *mapping_name = NULL;

		if (omap[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_WC) {
			data->wc_mapping_base = omap[i].mapping_base;
			mapping_name = "wc_mapping_base";
		} else if (omap[i].mapping_id == TENSTORRENT_MAPPING_RESOURCE0_UC) {
			data->uc_mapping_base = omap[i].mapping_base;
			mapping_name = "uc_mapping_base";
		}

		if (mapping_name != NULL) {
			D(2, "%s: id: %u base: 0x%010" PRIx64 " size: 0x%" PRIx64, mapping_name,
			  omap[i].mapping_id, omap[i].mapping_base, omap[i].mapping_size);
		}

		if (omap[i].mapping_size == 0) {
			continue;
		}
	}

	return 0;
}

static void close_tt_dev(struct tlb_data *data)
{
	if (data->fd == -1) {
		/* not yet opened or already closed */
		return;
	}

	if (close(data->fd) < 0) {
		E("fd %d: %s", data->fd, strerror(errno));
		return;
	}

	D(1, "closed fd %d", data->fd);

	data->fd = -1;
}

/*
 * Map the 2MiB TLB window. This can remain mapped for the duration of the
 * application. We simply change where the TLB window points by writing to the TLB config
 * register.
 */
static int map_tlb(struct tlb_data *data)
{
	if (data->tlb != MAP_FAILED) {
		/* already mapped? cons improperly initialized? */
		return 0;
	}

	uint64_t offset = data->tlb_id * TLB_2M_WINDOW_SIZE;

	data->tlb = mmap(NULL, TLB_2M_WINDOW_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, data->fd,
			 data->uc_mapping_base + offset);
	if (data->tlb == MAP_FAILED) {
		E("%s", strerror(errno));
		return -errno;
	}

	D(1, "mapped %zu@%08x to %zu@%p for 2MiB TLB window %d", (size_t)TLB_2M_WINDOW_SIZE,
	  (uint32_t)offset, (size_t)TLB_2M_WINDOW_SIZE, data->tlb, data->tlb_id);

	return 0;
}

static void unmap_tlb(struct tlb_data *data)
{
	if (data->tlb == MAP_FAILED) {
		/* not currently mapped */
		return;
	}

	if (munmap((void *)data->tlb, TLB_2M_WINDOW_SIZE) < 0) {
		E("%s", strerror(errno));
		return;
	}

	D(1, "unmapped %zu@%p", (size_t)TLB_2M_WINDOW_SIZE, data->tlb);

	data->tlb = MAP_FAILED;
}

static int map_tlb_regs(struct tlb_data *data)
{
	/*
	 * assert that TLB_CONFIG_ADDR is already aligned
	 * means we don't need an 'adjust' or 'offset' variables
	 */
	_Static_assert((TLB_CONFIG_ADDR & PAGE_SIZE) == 0, "Invalid tlb config addr");

	if (data->tlb_regs != MAP_FAILED) {
		/* already mapped? cons improperly initialized? */
		return 0;
	}

	data->tlb_regs = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, data->fd,
			      TLB_CONFIG_ADDR);
	if (data->tlb_regs == MAP_FAILED) {
		E("%s", strerror(errno));
		return -EIO;
	}

	D(1, "mapped %zu@%08x to %zu@%p", (size_t)PAGE_SIZE, TLB_CONFIG_ADDR, (size_t)PAGE_SIZE,
	  data->tlb_regs);

	if (verbose > 0) {
		for (int i = 0; i < BH_NUM_TLBS; ++i) {
			volatile uint32_t *reg_data = (volatile uint32_t *)&data->tlb_regs[i];

			if ((reg_data[0] == 0) && (reg_data[1] == 0) && (reg_data[2] == 0)) {
				continue;
			}

			if ((reg_data[0] == (uint32_t)-1) && (reg_data[1] == (uint32_t)-1) &&
			    (reg_data[2] == (uint32_t)-1)) {
				continue;
			}

			D(2, "tlb[%u]: %s", i, tlb2m2str(&data->tlb_regs[i]));
		}
	}

	return 0;
}

static void unmap_tlb_regs(struct tlb_data *data)
{
	if (data->tlb_regs == MAP_FAILED) {
		/* not currently mapped */
		return;
	}

	if (munmap((void *)data->tlb_regs, PAGE_SIZE) < 0) {
		E("%s", strerror(errno));
		return;
	}

	D(1, "unmapped %zu@%p", (size_t)PAGE_SIZE, data->tlb_regs);

	data->tlb_regs = MAP_FAILED;
}

int tlb_read(uint32_t addr, uint8_t *buf, size_t len)
{
	uint64_t adjust = program_noc(&_tlb_data, ARC_X, ARC_Y, TLB_ORDER_STRICT, addr);
	uint32_t *virt = (uint32_t *)(_tlb_data.tlb + adjust);

	D(2, "read from (%p,%p) (phys,virt)", (void *)(uintptr_t)addr, virt);
	memcpy(buf, virt, len);
	return 0;
}

int tlb_write(uint32_t addr, const uint8_t *buf, size_t len)
{
	uint64_t adjust = program_noc(&_tlb_data, ARC_X, ARC_Y, TLB_ORDER_STRICT, addr);
	uint32_t *virt = (uint32_t *)(_tlb_data.tlb + adjust);

	D(2, "write to (%p,%p) (phys,virt)", (void *)(uintptr_t)addr, virt);
	memcpy(virt, buf, len);
	return 0;
}

int tlb_init(void *data)
{
	int ret;
	struct tlb_init_data *init_data = (struct tlb_init_data *)data;

	_tlb_data.dev_name = init_data->dev_name;
	_tlb_data.pci_device_id = init_data->pci_device_id;
	_tlb_data.tlb_id = init_data->tlb_id;
	_tlb_data.fd = -1;
	_tlb_data.tlb = MAP_FAILED;
	_tlb_data.tlb_regs = MAP_FAILED;
	verbose = init_data->verbose;

	ret = open_tt_dev(&_tlb_data);
	if (ret < 0) {
		return ret;
	}

	ret = map_tlb_regs(&_tlb_data);
	if (ret < 0) {
		return ret;
	}

	ret = map_tlb(&_tlb_data);
	if (ret < 0) {
		return ret;
	}
	return ret;
}

void tlb_exit(void)
{
	unmap_tlb(&_tlb_data);
	unmap_tlb_regs(&_tlb_data);
	close_tt_dev(&_tlb_data);
}
