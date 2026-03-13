/**
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TT_ZEPHYR_PLATFORMS_LIB_TENSTORRENT_OCCP_OCCP_PRIVATE_H_
#define TT_ZEPHYR_PLATFORMS_LIB_TENSTORRENT_OCCP_OCCP_PRIVATE_H_

#define OCCP_APP_BASE             0x0
#define OCCP_BASE_MSG_GET_VERSION 0x0
#define OCCP_BASE_MSG_WRITE_DATA  0x2
#define OCCP_BASE_MSG_READ_DATA   0x3

#define OCCP_APP_BOOT               0x1
#define OCCP_BOOT_MSG_EXECUTE_IMAGE 0x1

#define OCCP_MAX_MSG_SIZE 255

struct occp_cmd_header {
	uint8_t app_id: 8;
	uint8_t msg_id: 8;
	uint8_t flags: 5;
	uint16_t length: 11;
} __packed;

struct occp_header {
	uint8_t header_crc: 8;
	bool body_crc_present: 1;
	uint32_t i3c_flags: 23;
	struct occp_cmd_header cmd_header;
} __packed;

struct occp_get_version_response {
	struct occp_header header;
	uint8_t major_version: 8;
	uint8_t minor_version: 8;
	uint32_t patch_version: 16;
	uint8_t body_crc: 8;
} __packed;

struct occp_write_data_request {
	struct occp_header header;
	uint32_t address_low;
	uint32_t address_high;
	uint32_t length: 11;
	uint32_t attributes: 5;
	uint32_t reserved: 16;
} __packed;

struct occp_read_data_request {
	struct occp_header header;
	uint32_t address_low;
	uint32_t address_high;
	uint32_t length: 11;
	uint32_t attributes: 5;
	uint32_t reserved: 16;
} __packed;

struct occp_execute_image_request {
	struct occp_header header;
	uint32_t execution_address_low;
	uint32_t execution_address_high;
	uint32_t cpu_id: 8;
	uint32_t reserved: 3;
	uint32_t attributes: 5;
} __packed;

struct occp_execute_image_response {
	struct occp_header header;
} __packed;

#endif /* TT_ZEPHYR_PLATFORMS_LIB_TENSTORRENT_OCCP_OCCP_PRIVATE_H_ */
