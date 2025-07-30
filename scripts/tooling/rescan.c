/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "logging.h"

#define TENSTORRENT_IOCTL_MAGIC        0xFA
#define TENSTORRENT_IOCTL_RESET_DEVICE _IO(TENSTORRENT_IOCTL_MAGIC, 6)

#define TENSTORRENT_PCI_VENDOR_ID 0x1e52

#define PCI_DEVICES_PATH "/sys/bus/pci/devices"

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

/* tenstorrent_reset_device_in.flags */
#define TENSTORRENT_RESET_DEVICE_RESTORE_STATE   0
#define TENSTORRENT_RESET_DEVICE_RESET_PCIE_LINK 1
#define TENSTORRENT_RESET_DEVICE_CONFIG_WRITE    2

struct tenstorrent_reset_device_inp {
	uint32_t output_size_bytes;
	uint32_t flags;
};

struct tenstorrent_reset_device_out {
	uint32_t output_size_bytes;
	uint32_t result;
};

struct tenstorrent_reset_device {
	struct tenstorrent_reset_device_inp in;
	struct tenstorrent_reset_device_out out;
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

static int pcie_remove(void)
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

static int pcie_rescan(void)
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

static int pcie_rescan_ioctl(const char *tt_dev_name)
{
	int ret, fd;

	/* First, try to rescan the device using IOCTL */
	struct tenstorrent_reset_device reset = {
		.in.output_size_bytes = sizeof(reset.out),
		.in.flags = TENSTORRENT_RESET_DEVICE_RESTORE_STATE,
	};

	fd = open(tt_dev_name, O_RDWR);
	if (fd < 0) {
		E("Failed to open device %s: %s", tt_dev_name, strerror(errno));
		return -errno;
	}

	ret = ioctl(fd, TENSTORRENT_IOCTL_RESET_DEVICE, &reset);
	if (ret < 0) {
		E("Failed to reset device: %s", strerror(errno));
		ret = -errno;
		goto out;
	}

	ret = pcie_walk_sysfs(TENSTORRENT_PCI_VENDOR_ID, 0xffff, pcie_count_cb, NULL);
out:
	close(fd);
	return ret;
}

static int pcie_rescan_sysfs(void)
{
	(void)pcie_remove();
	return pcie_rescan();
}

/**
 * Rescan the PCIe bus for Tenstorrent devices.
 *
 * @param tt_dev_name Name of the Tenstorrent device to rescan.
 * @return 0 on success, or a negative error code on failure.
 */
int rescan_pcie(const char *tt_dev_name)
{
	/* Try rescan via IOCTL, and fall back to sysfs if that fails */
	return pcie_rescan_ioctl(tt_dev_name) || pcie_rescan_sysfs();
}
