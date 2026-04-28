/*
 * Copyright (c) 2026 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_msg.h>
#include <string.h>
#include <tenstorrent/smc_msg.h>
#include <tenstorrent/msgqueue.h>
#include <zephyr/logging/log_backend_std.h>

#include "noc2axi.h"

/* Forward declaration for PCIe MSI function */
void SendPcieMsi(uint8_t pcie_inst, uint32_t vector_id);

LOG_MODULE_REGISTER(fw_tt_pcie_log_backend, CONFIG_LOG_DEFAULT_LEVEL);

/* tt_pcie_log sub-commands */
#define TT_PCIE_LOG_SUBCMD_SETUP   1
#define TT_PCIE_LOG_SUBCMD_RELEASE 2

/* Log entry header structure (must match KMD definition) */
struct fw_log_entry_header {
	uint16_t msg_size;  /* Size of log message including header */
	uint8_t log_level;  /* FW log level */
	uint8_t source;     /* 0=SMC, 1=DMC */
	uint32_t timestamp; /* FW timestamp in milliseconds */
	uint32_t sequence;  /* Sequence number */
} __packed;

/* Buffer header at start of shared buffer (must match KMD definition) */
struct fw_log_buffer_header {
	uint32_t write_offset; /* Current write position (circular) */
	uint32_t buffer_size;  /* Total buffer size */
	uint32_t magic;        /* Magic number for validation */
	uint32_t reserved;     /* Future use */
} __packed;

#define FW_LOG_SOURCE_SMC 0
#define FW_LOG_SOURCE_DMC 1

#define FW_LOG_BUFFER_MAGIC   0x544C4F47 /* "TLOG" */
#define FW_LOG_DATA_READY     0x1        /* Flag: firmware has new data */
#define FW_LOG_DATA_PROCESSED 0x0        /* Flag: host has processed data */

/* Static variables to track tt_pcie_log state */
static bool host_available; /* True when host buffer is set up */
static uint64_t log_buffer_iova;
static uint32_t log_buffer_size;
static uint32_t sequence_number;

/* NOC2AXI TLB configuration for direct host memory access */
#define PCIE_X 19 /* PCIe NOC coordinates as specified by user */
#define PCIE_Y 24

static void flush_buffer_to_host(void);

/* Buffer for accumulating framed log entries before host transmission */
static uint8_t framed_log_buffer[CONFIG_TT_PCIE_LOG_BACKEND_BUFFER_SIZE];
static uint32_t framed_buffer_pos; /* Current write position in framed buffer */

/* Timer for periodic buffer flushing */
static struct k_timer flush_timer;

/* Work item for buffer flushing */
static struct k_work flush_work;

/* Mutex to protect framed_log_buffer access between backend and timer */
static struct k_mutex buffer_mutex;

/**
 * @brief Work handler to flush framed buffer to host
 * @param work Work item (unused)
 */
static void flush_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	/* Lock mutex to prevent collision with backend_process */
	if (k_mutex_lock(&buffer_mutex, K_MSEC(100)) != 0) {
		/* Timeout - skip this flush cycle */
		return;
	}

	flush_buffer_to_host();

	k_mutex_unlock(&buffer_mutex);
}

/**
 * @brief Timer callback to submit flush work
 * @param timer Timer object (unused)
 */
static void flush_timer_callback(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* Submit work item to system work queue */
	k_work_submit(&flush_work);
}

/**
 * @brief Setup host tt_pcie_log by configuring NOC2AXI TLB and transferring local buffer
 * @param buffer_addr_lo Lower 32 bits of host buffer IOVA
 * @param buffer_addr_hi Upper 32 bits of host buffer IOVA
 * @param buffer_size Size of buffer in bytes
 * @return 0 on success, error code on failure
 */
static uint8_t setup_tt_pcie_log_backend(uint32_t buffer_addr_lo, uint32_t buffer_addr_hi,
					 uint32_t buffer_size)
{
	if (buffer_size <
	    sizeof(struct fw_log_buffer_header) + sizeof(struct fw_log_entry_header) + 1) {
		LOG_ERR("Host buffer too small: %u bytes", buffer_size);
		return 1; /* EPERM - buffer too small */
	}

	/* Lock mutex to prevent collision with backend_process */
	int res = k_mutex_lock(&buffer_mutex, K_MSEC(100));

	if (res != 0) {
		/* Timeout - skip this flush cycle */
		return res;
	}

	struct fw_log_buffer_header header;
	uint64_t new_buffer_iova = ((uint64_t)buffer_addr_hi << 32) | buffer_addr_lo;

	if (host_available) {
		LOG_INF("Host tt_pcie_log re-setup: old=0x%llx, new=0x%llx", log_buffer_iova,
			new_buffer_iova);
	}

	/* Update to new buffer (could be same address if host reused buffer) */
	log_buffer_iova = new_buffer_iova;
	log_buffer_size = buffer_size;

	/* Setup NOC2AXI TLB mapping for host memory access */
	/* Use TLB 0 for logging (should be available) */
	uint8_t noc2axi_tlb = 0;

	NOC2AXITlbSetup(0, noc2axi_tlb, PCIE_X, PCIE_Y, log_buffer_iova);

	/* Initialize buffer header */
	header.write_offset = sizeof(struct fw_log_buffer_header);
	header.buffer_size = buffer_size;
	header.magic = FW_LOG_BUFFER_MAGIC;
	header.reserved = 0;

	/* Write header to start of buffer */
	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 0, header.write_offset);
	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 4, header.buffer_size);
	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 8, header.magic);
	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 12, FW_LOG_DATA_PROCESSED);

	/* Mark host as available */
	host_available = true;

	/* Initialize timer */
	k_timer_start(&flush_timer, K_SECONDS(1), K_SECONDS(1));

	k_mutex_unlock(&buffer_mutex);

	LOG_INF("Host tt_pcie_log enabled: buffer=0x%llx, size=%u bytes", log_buffer_iova,
		buffer_size);
	return 0;
}

/**
 * @brief Release host tt_pcie_log and revert to local buffer only
 * @return 0 on success
 */
static uint8_t release_tt_pcie_log_backend(void)
{
	if (!host_available) {
		LOG_WRN("Host tt_pcie_log not enabled, ignoring release request");
		return 0; /* Not enabled, not an error */
	}

	int res = k_mutex_lock(&buffer_mutex, K_MSEC(100));

	if (res != 0) {
		/* Timeout - skip this flush cycle */
		return res;
	}
	/* Stop the flush timer */
	k_timer_stop(&flush_timer);

	host_available = false;
	log_buffer_iova = 0;
	log_buffer_size = 0;
	k_mutex_unlock(&buffer_mutex);

	LOG_INF("Host tt_pcie_log disabled - reverting to local buffer");
	return 0;
}

/**
 * @brief Flush accumulated framed messages to host
 */
static void flush_buffer_to_host(void)
{
	uint32_t data_ready_flag;
	uint32_t magic_value;

	if (framed_buffer_pos == 0 || !host_available) {
		return; /* Nothing to flush or host not available */
	}

	/* Setup NOC2AXI TLB for host memory access */
	uint8_t noc2axi_tlb = 0;

	NOC2AXITlbSetup(0, noc2axi_tlb, PCIE_X, PCIE_Y, log_buffer_iova);

	/* Check buffer magic number first - if corrupted, don't proceed */
	magic_value = NOC2AXIRead32(0, noc2axi_tlb, log_buffer_iova + 8);
	if (magic_value != FW_LOG_BUFFER_MAGIC) {
		/* Buffer corrupted or invalid, reset position and return */
		framed_buffer_pos = 0;
		return;
	}

	/* Check if host has processed previous data */
	data_ready_flag = NOC2AXIRead32(0, noc2axi_tlb, log_buffer_iova + 12);
	if (data_ready_flag == FW_LOG_DATA_READY) {
		/* Host hasn't processed previous data yet, drop this batch */
		framed_buffer_pos = 0;
		return;
	}

	/* Write entire framed buffer to host starting after header */
	uint32_t write_addr = log_buffer_iova + sizeof(struct fw_log_buffer_header);
	/*
	 * Clamp the amount we're writing to the available host log buffer data space
	 * (total buffer size minus header size). We should look into doing this better,
	 * e.g. as a ring buffer of framed messages, we could later send the additional buffer
	 *
	 * In practice, expectation is that FW buffer size is less than or equal to host's.
	 */
	uint32_t available_host_space = log_buffer_size - sizeof(struct fw_log_buffer_header);
	uint32_t bytes_to_write = MIN(framed_buffer_pos, available_host_space);

	if (framed_buffer_pos > available_host_space) {
		LOG_WRN("Truncating log data: %u bytes -> %u bytes (host space limit)",
			framed_buffer_pos, bytes_to_write);
	}

	/* Write in 32-bit word chunks for efficiency */
	for (uint32_t i = 0; i < bytes_to_write; i += 4) {
		uint32_t word_data = 0;
		uint32_t bytes_to_copy = MIN(4, bytes_to_write - i);

		/* Pack up to 4 bytes into a 32-bit word */
		memcpy(&word_data, &framed_log_buffer[i], bytes_to_copy);
		NOC2AXIWrite32(0, noc2axi_tlb, write_addr + i, word_data);
	}

	/* Update write_offset to reflect data size */
	uint32_t new_write_offset = sizeof(struct fw_log_buffer_header) + bytes_to_write;

	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 0, new_write_offset);

	/* Set flag to indicate new data is ready */
	NOC2AXIWrite32(0, noc2axi_tlb, log_buffer_iova + 12, FW_LOG_DATA_READY);

	/* Trigger interrupt to notify host of new data */
	SendPcieMsi(0, 0);

	/* Reset framed buffer after successful write */
	framed_buffer_pos = 0;
}

/**
 * @brief Character output function for Zephyr log formatting - writes directly to 4KB buffer
 * @param data Data to append to framed log buffer
 * @param length Length of data
 * @param ctx Context (unused)
 * @return Length processed
 */
static int putc_in_frame(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);

	/* Copy data directly to framed_log_buffer at current position */
	size_t bytes_to_copy = MIN(length, sizeof(framed_log_buffer) - framed_buffer_pos);

	if (bytes_to_copy > 0) {
		memcpy(framed_log_buffer + framed_buffer_pos, data, bytes_to_copy);
		framed_buffer_pos += bytes_to_copy;
	}

	return bytes_to_copy;
}

static char buf[1];
LOG_OUTPUT_DEFINE(log_output_tt_pcie, putc_in_frame, buf, 1);

/**
 * @brief Zephyr log backend process function - called for each individual log message
 * @param backend The backend instance
 * @param msg The log message to process
 */
static void backend_process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	struct fw_log_entry_header entry_header;
	uint32_t flags;
	log_format_func_t log_output_func;
	uint32_t entry_start_pos;
	uint32_t message_start_pos;

	/* Lock mutex to prevent collision with flush timer */
	if (k_mutex_lock(&buffer_mutex, K_MSEC(10)) != 0) {
		/* Timeout - drop this data */
		return;
	}

	/* Check if we need to flush buffer for space */
	if (framed_buffer_pos + sizeof(entry_header) + 256 > sizeof(framed_log_buffer)) {
		flush_buffer_to_host();
	}

	/* Verify there's enough space for at least the header after flush */
	if (framed_buffer_pos + sizeof(entry_header) > sizeof(framed_log_buffer)) {
		/* Not enough space even for header - drop this message */
		k_mutex_unlock(&buffer_mutex);
		return;
	}

	/* Mark where this entry starts */
	entry_start_pos = framed_buffer_pos;

	/* Create header and reserve space for it */
	entry_header.msg_size = 0; /* Will be updated after formatting */
	entry_header.log_level = log_msg_get_level(&msg->log);
	entry_header.source = FW_LOG_SOURCE_SMC;
	entry_header.timestamp = msg->log.hdr.timestamp;
	entry_header.sequence = sequence_number++;

	/* Write header to buffer and advance position */
	memcpy(framed_log_buffer + framed_buffer_pos, &entry_header, sizeof(entry_header));
	framed_buffer_pos += sizeof(entry_header);
	message_start_pos = framed_buffer_pos;

	/* Use Zephyr's log formatting system to write directly into framed_log_buffer */
	flags = log_backend_std_get_flags();
	log_output_func = log_format_func_t_get(LOG_OUTPUT_TEXT);
	log_output_func(&log_output_tt_pcie, &msg->log, flags);

	/* Calculate actual sizes and update header */
	uint32_t message_len = framed_buffer_pos - message_start_pos;
	uint32_t total_size = sizeof(entry_header) + message_len;
	((struct fw_log_entry_header *)(framed_log_buffer + entry_start_pos))->msg_size =
		total_size;

	k_mutex_unlock(&buffer_mutex);
}

/**
 * @brief Zephyr log backend panic function - called when system panics
 * @param backend The backend instance
 */
static void backend_panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);

	/* Try to acquire mutex with a short timeout during panic */
	if (k_mutex_lock(&buffer_mutex, K_MSEC(10)) == 0) {
		flush_buffer_to_host();
		k_mutex_unlock(&buffer_mutex);
	}
	/* If mutex lock fails during panic, skip flush to avoid further issues */
}

/**
 * @brief Zephyr log backend dropped function - called when messages are dropped
 * @param backend The backend instance
 * @param cnt Number of dropped messages
 */
static void backend_dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	ARG_UNUSED(cnt);
}

/**
 * @brief Initialize the Zephyr log backend
 * @param backend The backend instance
 */
static void backend_init(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	k_mutex_init(&buffer_mutex);
	k_work_init(&flush_work, flush_work_handler);
	k_timer_init(&flush_timer, flush_timer_callback, NULL);
}

/**
 * @brief Handles the request to setup or release FW tt_pcie_log
 * @param[in] request The request, of type @ref tt_pcie_log_rqst, with command code @ref
 * TT_SMC_MSG_TT_PCIE_LOG
 * @param[out] response The response to the host
 * @return 0 for success, error code for failure
 */
static uint8_t tt_pcie_log_handler(const union request *request, struct response *response)
{
	uint8_t result = 0;

	switch (request->tt_pcie_log.subcmd) {
	case TT_PCIE_LOG_SUBCMD_SETUP:
		result = setup_tt_pcie_log_backend(request->tt_pcie_log.buffer_addr_lo,
						   request->tt_pcie_log.buffer_addr_hi,
						   request->tt_pcie_log.buffer_size);
		break;

	case TT_PCIE_LOG_SUBCMD_RELEASE:
		result = release_tt_pcie_log_backend();
		break;

	default:
		LOG_ERR("Unknown tt_pcie_log sub-command: %u", request->tt_pcie_log.subcmd);
		result = 22; /* EINVAL - invalid argument */
		break;
	}

	return result;
}

/* Define the Zephyr log backend API */
static struct log_backend_api backend_api = {
	.process = backend_process,
	.panic = backend_panic,
	.init = backend_init,
	.dropped = backend_dropped,
};

/* Register the message handler for tt_pcie_log commands */
REGISTER_MESSAGE(TT_SMC_MSG_TT_PCIE_LOG, tt_pcie_log_handler);

/* Define and initialize the log backend - always enabled */
LOG_BACKEND_DEFINE(backend_tt_pcie_log, backend_api, true);
