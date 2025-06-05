/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdio.h>
#include <string.h>

#include "arc_jtag.h"
#include "arc_tlb.h"

#define BH_SCRAPPY_PCI_DEVICE_ID  0xb140
#define TT_DEVICE "/dev/tenstorrent/0"

/* Definition of functions used to access memory */
struct mem_access_driver {
	int (*start)(void *init_data);
	int (*read)(uint32_t addr, uint8_t *buf, size_t len);
	int (*write)(uint32_t addr, const uint8_t *buf, size_t len);
	void (*stop)(void);
};

struct mem_access_driver tlb_driver = {
	.start = tlb_init,
	.read = tlb_read,
	.write = tlb_write,
	.stop = tlb_exit,
};

static struct jtag_init_data jtag_init_data = {
	.verbose = 0,
	.serial_number = NULL,
};
static struct tlb_init_data tlb_init_data = {
	.dev_name = TT_DEVICE,
	.pci_device_id = BH_SCRAPPY_PCI_DEVICE_ID,
	.tlb_id = BH_2M_TLB_UC_DYNAMIC_START + 1,
	.verbose = 0,
};

struct mem_access_driver jtag_driver = {
	.start = arc_jtag_init,
	.read = arc_jtag_read_mem,
	.write = arc_jtag_write_mem,
	.stop = arc_jtag_exit,
};

#define TEST_MEM_ADDR 0x10000000 /* Start of CSM on ARC */

static int test_read(int (*read_func)(uint32_t, uint8_t *, size_t), uint32_t addr,
		uint8_t *buf, uint32_t len)
{
	int ret = read_func(addr, buf, len);

	if (ret < 0) {
		fprintf(stderr, "Failed to read memory from address 0x%08X-0x%08X: %d\n",
			addr, addr + len, ret);
	}
	return ret;
}

static int test_write(int (*write_func)(uint32_t, const uint8_t *, size_t), uint32_t addr,
		uint8_t *buf, uint32_t len)
{
	int ret = write_func(addr, buf, len);

	if (ret < 0) {
		fprintf(stderr, "Failed to write memory to address 0x%08X-0x%08X: %d\n",
			addr, addr + len, ret);
	}
	return ret;
}

static int test_memory(void *init_data, struct mem_access_driver *driver)
{
	uint8_t test_buf[128];
	uint8_t check_buf[128];
	int ret;

	if (driver->start(init_data) < 0) {
		fprintf(stderr, "Failed to initialize memory access driver\n");
		return -1;
	}

	ret = test_read(driver->read, TEST_MEM_ADDR, test_buf, sizeof(test_buf));
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR, check_buf, 4);
	if (ret < 0) {
		goto out;
	}

	if (memcmp(test_buf, check_buf, 4) != 0) {
		fprintf(stderr, "Memory read data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}

	/* Perform unaligned memory reads */
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR, check_buf, 1);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, 1) != 0) {
		fprintf(stderr, "Memory read data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR + 1, check_buf, 4);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf + 1, check_buf, 4) != 0) {
		fprintf(stderr, "Memory read data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}

	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR + 1, check_buf, 3);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf + 1, check_buf, 3) != 0) {
		fprintf(stderr, "Memory read data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}

	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR + 1, check_buf, 10);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf + 1, check_buf, 10) != 0) {
		fprintf(stderr, "Memory read data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}

	/* Test memory writes */
	for (int i = 0; i < sizeof(test_buf); i++) {
		test_buf[i] = i;
	}
	ret = test_write(driver->write, TEST_MEM_ADDR, test_buf, sizeof(test_buf));
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR, check_buf, sizeof(test_buf));
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, sizeof(test_buf)) != 0) {
		fprintf(stderr, "Memory write data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}
	for (int i = 0; i < sizeof(test_buf); i++) {
		test_buf[i] = i + 1;
	}
	ret = test_write(driver->write, TEST_MEM_ADDR, test_buf, 4);
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR, check_buf, 4);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, 4) != 0) {
		fprintf(stderr, "Memory write data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}
	for (int i = 0; i < sizeof(test_buf); i++) {
		test_buf[i] = i + 2;
	}
	ret = test_write(driver->write, TEST_MEM_ADDR, test_buf, 1);
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR, check_buf, 1);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, 1) != 0) {
		fprintf(stderr, "Memory write data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}
	for (int i = 0; i < sizeof(test_buf); i++) {
		test_buf[i] = i + 3;
	}
	ret = test_write(driver->write, TEST_MEM_ADDR + 1, test_buf, 4);
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR + 1, check_buf, 4);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, 4) != 0) {
		fprintf(stderr, "Memory write data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}
	for (int i = 0; i < sizeof(test_buf); i++) {
		test_buf[i] = i + 4;
	}
	ret = test_write(driver->write, TEST_MEM_ADDR + 1, test_buf, 10);
	if (ret < 0) {
		goto out;
	}
	memset(check_buf, 0, sizeof(check_buf));
	ret = test_read(driver->read, TEST_MEM_ADDR + 1, check_buf, 10);
	if (ret < 0) {
		goto out;
	}
	if (memcmp(test_buf, check_buf, 10) != 0) {
		fprintf(stderr, "Memory write data mismatch: %d\n", __LINE__);
		ret = -1;
		goto out;
	}

out:
	driver->stop();
	return ret;
}

int main(int argc, char **argv)
{
	if (test_memory(&jtag_init_data, &jtag_driver) < 0) {
		fprintf(stderr, "Failed to test memory using JTAG driver\n");
		return -1;
	}
	fprintf(stderr, "Successfully tested memory using JTAG driver\n");
	if (test_memory(&tlb_init_data, &tlb_driver) < 0) {
		fprintf(stderr, "Failed to test memory using TLB driver\n");
		return -1;
	}
	fprintf(stderr, "Successfully tested memory using TLB driver\n");
	return 0;
}
