/**
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define _XOPEN_SOURCE 500

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>
#include <limits.h>

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

struct pcie_walk_data {
	uint16_t match_vid;
	uint16_t match_pid;
	int (*cb)(char *path, size_t path_size, void *data);
	void *user_data;
	unsigned int counter;
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

static struct pcie_walk_data *g_walk_data;

static int pcie_nftw_callback(const char *fpath, const struct stat *sb, int typeflag,
			      struct FTW *ftwbuf)
{
	int ret;
	unsigned long pid, vid;
	char vendor_path[PATH_MAX];
	char device_path[PATH_MAX];
	char device_path_copy[PATH_MAX];
	(void)sb;
	(void)ftwbuf;

	/* Only process symbolic links (PCIe devices) */
	if (typeflag != FTW_SL) {
		D(2, "Skipping non-symlink %s", fpath);
		return 0;
	}

	if (strstr(fpath, "/sys/bus/pci/devices/") != fpath) {
		D(2, "Skipping non-PCI device %s", fpath);
		return 0;
	}

	/* Build vendor file path */
	snprintf(vendor_path, sizeof(vendor_path), "%s/vendor", fpath);
	ret = pcie_read_unsigned_long_from_file(vendor_path, sizeof(vendor_path), &vid);
	if (ret != 0) {
		D(2, "Could not read vendor id from %s", vendor_path);
		return 0;
	}

	/* Build device file path */
	snprintf(device_path, sizeof(device_path), "%s/device", fpath);
	ret = pcie_read_unsigned_long_from_file(device_path, sizeof(device_path), &pid);
	if (ret != 0) {
		D(2, "Could not read device id from %s", device_path);
		return 0;
	}

	/* Check if vendor/product IDs match */
	if (((vid == g_walk_data->match_vid) || (g_walk_data->match_vid == 0xffff)) &&
	    ((pid == g_walk_data->match_pid) || (g_walk_data->match_pid == 0xffff))) {

		D(1, "Found %s with vendor id %04lx and product id %04lx", fpath, vid, pid);

		strncpy(device_path_copy, fpath, sizeof(device_path_copy));
		device_path_copy[sizeof(device_path_copy) - 1] = '\0';

		ret = g_walk_data->cb(device_path_copy, sizeof(device_path_copy),
				      g_walk_data->user_data);
		if (ret < 0) {
			return -1; /* Stop walking on error */
		}
		g_walk_data->counter += ret;
	}

	return 0;
}

static int pcie_walk_sysfs(uint16_t match_vid, uint16_t match_pid,
			   int (*cb)(char *path, size_t path_size, void *data), void *data)
{
	struct pcie_walk_data walk_data = {.match_vid = match_vid,
					   .match_pid = match_pid,
					   .cb = cb,
					   .user_data = data,
					   .counter = 0};

	g_walk_data = &walk_data;

	int ret = nftw(PCI_DEVICES_PATH, pcie_nftw_callback, 20, FTW_PHYS);

	g_walk_data = NULL;

	if (ret != 0) {
		E("Failed to walk %s: %s", PCI_DEVICES_PATH, strerror(errno));
		return -errno;
	}

	return walk_data.counter;
}

static int pcie_remove_cb(char *path, size_t path_size, void *data)
{
	int ret;

	(void)data;

	strncat(path, "/remove", path_size);
	if (geteuid() == 0) {
		/* Root can remove devices without additional permissions */
		int fd = open(path, O_WRONLY);

		if (fd < 0) {
			E("Failed to open %s: %s", path, strerror(errno));
			return -errno;
		}

		if (write(fd, "1", 1) < 0) {
			E("Failed to write to %s: %s", path, strerror(errno));
			ret = -errno;
			goto out_root;
		}

out_root:
		close(fd);
		goto out;
	}

	size_t path_len = strlen(path);
	/* Non-root users can only remove devices with sudo */
	size_t shift_len = strlen("echo 1 | sudo tee ");

	if (path_size < path_len + shift_len + 1) {
		E("Path buffer too small. Have %zu, need %zu", path_size, path_len + shift_len + 1);
		return -ENOMEM;
	}

	D(2, "Modifying path '%s' for sudo command", path);
	memmove(&path[shift_len], path, path_len + 1);
	strncpy(path, "echo 1 | sudo tee", shift_len);

	D(2, "Running command '%s'", path);
	ret = system(path);
	if (ret != 0) {
		E("Command %s failed: %d", path, ret);
		ret = errno ? -errno : -EIO;
	}

	memmove(path, &path[shift_len], path_len);
	path[path_len] = '\0';

out:
	/* Remove the appended '/remove' string */
	path[strlen(path) - 7] = '\0';

	if (ret == 0) {
		D(1, "Removed PCIe device %s", path);
		ret = 1; /* success: count the number of devices removed */
	} else {
		D(1, "Failed to remove PCIe device %s", path);
	}

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
	int ret;

	if (geteuid() == 0) {
		/* Root can rescan devices without additional permissions */
		int fd = open("/sys/bus/pci/rescan", O_WRONLY);

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
	} else {
		/* Non-root users can rescan devices with sudo (or via ioctl) */
		static const char *cmd = "echo 1 | sudo tee /sys/bus/pci/rescan";

		D(2, "Running command '%s'", cmd);
		ret = system(cmd);
		if (ret != 0) {
			E("Command '%s' failed: %d", cmd, ret);
			ret = errno ? -errno : -EIO;
			return ret;
		}
	}

	ret = pcie_walk_sysfs(TENSTORRENT_PCI_VENDOR_ID, 0xffff, pcie_count_cb, NULL);

	if (ret > 0) {
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
		E_RL(1000, "Failed to open device %s: %s", tt_dev_name, strerror(errno));
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
	return (pcie_rescan_ioctl(tt_dev_name) > 0) || pcie_rescan_sysfs();
}
