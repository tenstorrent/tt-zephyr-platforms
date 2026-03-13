/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/** @file
 * @brief Open Chiplet Configuration Protocol (OCCP) implementation
 *
 * OCCP is a protocol used to communicate with chiplets during early boot.
 * It can support multiple transport layers, implemented as backends within
 * this subsystem.
 */

#include <tenstorrent/occp.h>
#include "occp_private.h"

#include <zephyr/sys/crc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(occp, CONFIG_OCCP_LOG_LEVEL);

uint8_t occp_rw_buffer[OCCP_MAX_MSG_SIZE];

static void fill_cmd_header(uint8_t app_id, uint8_t msg_id, uint16_t length,
			    struct occp_header *hdr)
{
	hdr->cmd_header.app_id = app_id;
	hdr->cmd_header.msg_id = msg_id;
	hdr->cmd_header.length = length;
	hdr->header_crc = crc8((uint8_t *)hdr + 1, sizeof(*hdr) - 1, 0xD3, 0xFF, false);
}

/**
 * @brief Get OCCP protocol version
 * @param backend OCCP backend to use
 * @param major Pointer to store major version
 * @param minor Pointer to store minor version
 * @param patch Pointer to store patch version
 * @return 0 on success, negative error code on failure
 */
int occp_get_version(const struct occp_backend *backend, uint8_t *major, uint8_t *minor,
		     uint8_t *patch)
{
	struct occp_header req = {0};
	struct occp_get_version_response version_resp = {0};
	int ret;

	/* Send a GET_VERSION command */
	fill_cmd_header(OCCP_APP_BASE, OCCP_BASE_MSG_GET_VERSION, 0, &req);
	ret = backend->send(backend, (uint8_t *)&req, sizeof(req));
	if (ret != 0) {
		LOG_ERR("Failed to send OCCP GET_VERSION command: %d", ret);
		return ret;
	}
	/* Read response */
	LOG_DBG("Reading OCCP GET_VERSION response");
	ret = backend->receive(backend, (uint8_t *)&version_resp, sizeof(version_resp));
	if (ret != 0) {
		LOG_ERR("Failed to read OCCP GET_VERSION response: %d", ret);
		return ret;
	}
	*major = version_resp.major_version;
	*minor = version_resp.minor_version;
	*patch = version_resp.patch_version;
	return 0;
}

/**
 * @brief Write data to OCCP device
 * @param backend OCCP backend to use
 * @param address Address to write to
 * @param data Pointer to data to write
 * @param length Length of data to write
 * @return 0 on success, negative error code on failure
 */
int occp_write_data(const struct occp_backend *backend, uint64_t address, const uint8_t *data,
		    size_t length)
{
	struct occp_write_data_request write_req = {0};
	struct occp_header resp;
	int ret;
	uint64_t write_addr = address;
	size_t write_length;
	/* Retry a transfer up to 10 times */
	int retry_cnt = 10;

	if (!IS_ALIGNED(address, 4)) {
		LOG_ERR("OCCP write address must be 4-byte aligned");
		return -EINVAL;
	}

	if (!IS_ALIGNED(length, 4)) {
		LOG_ERR("Write length must be a multiple of 4 bytes");
		return -EINVAL;
	}

	while (length > 0) {
		write_length = MIN(length, OCCP_MAX_MSG_SIZE - sizeof(write_req));
		write_length = ROUND_DOWN(write_length, 4); /* Align to 4 bytes */
		/* Issue a WRITE_DATA command */
		fill_cmd_header(OCCP_APP_BASE, OCCP_BASE_MSG_WRITE_DATA,
				sizeof(write_req) - sizeof(write_req.header) + write_length,
				&write_req.header);
		write_req.address_low = write_addr & GENMASK(31, 0);
		write_req.address_high = (write_addr >> 32);
		write_req.length = write_length;
		memcpy(occp_rw_buffer, &write_req, sizeof(write_req));
		memcpy(occp_rw_buffer + sizeof(write_req), data, write_length);
		LOG_DBG("Sending OCCP WRITE_DATA command: addr=0x%llx, length=%zu, remaining=%zu",
			write_addr, write_length, length);
		ret = backend->send(backend, occp_rw_buffer, sizeof(write_req) + write_length);
		if (ret != 0) {
			LOG_ERR("Failed to send OCCP WRITE_DATA command: %d", ret);
			return ret;
		}

		/* Now read the response */
		ret = backend->receive(backend, (uint8_t *)&resp, sizeof(resp));
		if (ret != 0) {
			LOG_ERR("Failed to read OCCP WRITE_DATA response: %d", ret);
			return ret;
		}
		if (resp.cmd_header.flags) {
			LOG_ERR("OCCP WRITE_DATA command failed with flags: 0x%02x",
				resp.cmd_header.flags);
			retry_cnt--;
			k_msleep(100);
			if (retry_cnt == 0) {
				return -EIO;
			}
		} else {
			retry_cnt = 10; /* Reset retry count on success */
			/* Advance to next chunk */
			write_addr += write_length;
			data += write_length;
			length -= write_length;
		}
	}
	return 0;
}

/**
 * @brief Read data from OCCP device
 * @param backend OCCP backend to use
 * @param address Address to read from
 * @param data Pointer to buffer to store read data
 * @param length Length of data to read
 * @return 0 on success, negative error code on failure
 */
int occp_read_data(const struct occp_backend *backend, uint64_t address, uint8_t *data,
		   size_t length)
{
	struct occp_read_data_request read_req = {0};
	struct occp_header *resp;
	int ret;
	uint64_t read_addr = address;
	size_t read_length, crc_len;

	if (!IS_ALIGNED(address, 4)) {
		LOG_ERR("OCCP read address must be 4-byte aligned");
		return -EINVAL;
	}

	if (!IS_ALIGNED(length, 4)) {
		LOG_ERR("Read length must be a multiple of 4 bytes");
		return -EINVAL;
	}

	while (length > 0) {
		read_length = MIN(length, OCCP_MAX_MSG_SIZE);
		read_length = ROUND_DOWN(read_length, 4); /* Align to 4 bytes */

		/* Issue a READ_DATA command */
		fill_cmd_header(OCCP_APP_BASE, OCCP_BASE_MSG_READ_DATA,
				sizeof(read_req) - sizeof(read_req.header), &read_req.header);
		read_req.address_low = read_addr & GENMASK(31, 0);
		read_req.address_high = (read_addr >> 32);
		read_req.length = read_length;

		LOG_DBG("Sending OCCP READ_DATA command: addr=0x%llx, length=%zu, remaining=%zu",
			read_addr, read_length, length);
		ret = backend->send(backend, (uint8_t *)&read_req, sizeof(read_req));
		if (ret != 0) {
			LOG_ERR("Failed to send OCCP READ_DATA command: %d", ret);
			return ret;
		}

		/*
		 * Calculate CRC len, as OCCP response includes it.
		 * Note we skip CRC checks
		 */
		crc_len = read_length > 13 ? 4 : 1;
		ret = backend->receive(backend, occp_rw_buffer,
				       sizeof(struct occp_header) + read_length + crc_len);
		if (ret != 0) {
			LOG_ERR("Failed to read OCCP READ_DATA header: %d", ret);
			return ret;
		}
		resp = (struct occp_header *)occp_rw_buffer;
		if (resp->cmd_header.flags) {
			LOG_ERR("OCCP READ_DATA command failed with flags: 0x%02x",
				resp->cmd_header.flags);
			return -EIO;
		}
		memcpy(data, occp_rw_buffer + sizeof(struct occp_header), read_length);
		/* Advance to next chunk */
		read_addr += read_length;
		data += read_length;
		length -= read_length;
	}
	return 0;
}

/**
 * @brief Execute image at specified address
 * @param backend OCCP backend to use
 * @param execution_address Address to execute image from
 * @param cpu_id CPU ID to execute on
 * @return 0 on success, negative error code on failure
 */
int occp_execute_image(const struct occp_backend *backend, uint64_t execution_address,
		       uint8_t cpu_id)
{
	struct occp_execute_image_request exec_req = {0};
	struct occp_execute_image_response exec_resp = {0};
	int ret;

	/* Issue an EXECUTE_IMAGE command */
	fill_cmd_header(OCCP_APP_BOOT, OCCP_BOOT_MSG_EXECUTE_IMAGE,
			sizeof(exec_req) - sizeof(exec_req.header), &exec_req.header);
	exec_req.execution_address_low = execution_address & GENMASK(31, 0);
	exec_req.execution_address_high = (execution_address >> 32);
	exec_req.cpu_id = cpu_id;
	ret = backend->send(backend, (uint8_t *)&exec_req, sizeof(exec_req));
	if (ret != 0) {
		LOG_ERR("Failed to send OCCP EXECUTE_IMAGE command: %d", ret);
		return ret;
	}
	/* Read execute image response */
	ret = backend->receive(backend, (uint8_t *)&exec_resp, sizeof(exec_resp));
	if (ret != 0) {
		LOG_ERR("Failed to read OCCP EXECUTE_IMAGE response: %d", ret);
		return ret;
	}
	if (exec_resp.header.cmd_header.flags) {
		LOG_ERR("OCCP EXECUTE_IMAGE command failed with flags: 0x%02x",
			exec_resp.header.cmd_header.flags);
		return -EIO;
	}
	return 0;
}
