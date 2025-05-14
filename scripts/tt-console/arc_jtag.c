/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <libjaylink/libjaylink.h>

#include "arc_jtag.h"

#define D(level, fmt, ...)                                                      \
	if (verbose >= level) {                                                 \
		printf("D: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__);        \
	}

#define E(fmt, ...) fprintf(stderr, "E: %s(): " fmt "\r\n", __func__, ##__VA_ARGS__)

#define I(fmt, ...)                                                             \
	if (verbose >= 0) {                                                     \
		printf(fmt "\r\n", ##__VA_ARGS__);                              \
	}

static struct jaylink_context *ctx;
static struct jaylink_device_handle *devh;
static int verbose;
static uint8_t caps[JAYLINK_DEV_EXT_CAPS_SIZE];
static struct jaylink_connection conn;

#define DIV_ROUND_UP(val, div) ((((val) + ((div) - 1))) / (div))

#define JTAG_QUEUE_SIZE 256 /* Number of JTAG states we can queue transations between */

/* ARC JTAG definitions */
#define ARC_IDCODE 0x201444B1

#define ARC_JTAG_STATUS_REG 0x8
#define ARC_TRANSACTION_CMD_REG 0x9
#define ARC_ADDRESS_REG 0xa
#define ARC_DATA_REG 0xb
#define ARC_IDCODE_REG 0xc
#define ARC_BYPASS_REG 0xf

#define ARC_TRANSACTION_WRITE_MEM 0x0
#define ARC_TRANSACTION_WRITE_REG 0x1
#define ARC_TRANSACTION_WRITE_AUX_REG 0x2
#define ARC_TRANSACTION_NOP 0x3
#define ARC_TRANSACTION_READ_MEM 0x4
#define ARC_TRANSACTION_READ_REG 0x5
#define ARC_TRANSACTION_READ_AUX_REG 0x6

struct tdo_buffer {
	uint8_t *buf;
	int bit_len;
	struct tdo_buffer *next;
};

/* Tracks queued JTAG transactions */
static struct {
	uint8_t tms[JTAG_QUEUE_SIZE / 8];
	uint8_t tdi[JTAG_QUEUE_SIZE / 8];
	uint8_t tdo[JTAG_QUEUE_SIZE / 8];
	uint8_t ir_len;
	uint8_t bypass_dr_len;
	uint8_t tap_count;
	uint32_t queue_idx;
	/* Linked list of output buffers */
	struct tdo_buffer *tdo_buf_list;
} jtag_queue;

/*
 * Performs a bitwise copy into `dst`. Copy is started from `src_offset` offset
 * in `src`, to the bit offset `dst_offset` in `dst`. `bit_len` bits are copied.
 */
static void bitcopy(uint8_t *dst, const uint8_t *src, int dst_offset, int src_offset, int bit_len)
{
	for (int i = 0; i < bit_len; i++) {
		int dst_bit_off = (dst_offset + i) % 8;
		int dst_byte_off = (dst_offset + i) / 8;
		int src_bit_off = (src_offset + i) % 8;
		int src_byte_off = (src_offset + i) / 8;

		if (src[src_byte_off] & (1 << src_bit_off)) {
			dst[dst_byte_off] |= (1 << dst_bit_off);
		} else {
			dst[dst_byte_off] &= ~(1 << dst_bit_off);
		}
	}
}

/*
 * Zeros out the bits in `dst` starting from `dst_offset` to `bit_len` bits.
 */
static void bitzero(uint8_t *dst, int dst_offset, int bit_len)
{
	for (int i = 0; i < bit_len; i++) {
		int bit_offset = (dst_offset + i) % 8;
		int byte_offset = (dst_offset + i) / 8;

		dst[byte_offset] &= ~(1 << bit_offset);
	}
}

static int jtag_queue_transaction(const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo, int bit_len)
{
	struct tdo_buffer *buf;

	if (jtag_queue.queue_idx + bit_len > JTAG_QUEUE_SIZE) {
		E("JTAG queue overflow");
		return -ENOBUFS;
	}

	/* Enqueue the TMS, TDI, and TDO data */
	if (tms) {
		bitcopy(jtag_queue.tms, tms, jtag_queue.queue_idx, 0, bit_len);
	} else {
		bitzero(jtag_queue.tms, jtag_queue.queue_idx, bit_len);
	}
	if (tdi) {
		bitcopy(jtag_queue.tdi, tdi, jtag_queue.queue_idx, 0, bit_len);
	} else {
		bitzero(jtag_queue.tdi, jtag_queue.queue_idx, bit_len);
	}
	buf = malloc(sizeof(struct tdo_buffer));
	if (!buf) {
		E("Failed to allocate memory for TDO buffer");
		return -ENOMEM;
	}
	buf->buf = tdo;
	buf->bit_len = bit_len;
	buf->next = NULL;
	/* Append this buffer to the linked list */
	if (jtag_queue.tdo_buf_list == NULL) {
		jtag_queue.tdo_buf_list = buf;
	} else {
		struct tdo_buffer *cur = jtag_queue.tdo_buf_list;

		while (cur->next) {
			cur = cur->next;
		}
		cur->next = buf;
	}

	jtag_queue.queue_idx += bit_len;

	return 0;
}

static int jtag_enqueue_write_ir(uint8_t *data, int tap_idx, int bit_len, bool return_to_idle)
{
	int ret;
	uint8_t tms[DIV_ROUND_UP(bit_len, 8)];
	uint8_t tdi;

	/* Enqueue transation into SHIFT-IR state. Assume we start in run/test idle */
	tms[0] = 0x3;
	ret = jtag_queue_transaction(tms, NULL, NULL, 3);
	if (ret < 0) {
		return ret;
	}
	/* Now, enqueue IR writes for the TAPs in bypass before this one */
	memset(tms, 0, sizeof(tms));
	for (int i = 0; i < jtag_queue.tap_count; i++) {
		if (i == tap_idx) {
			/* Enqueue the IR write */
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit IR write */
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 1 << ((bit_len - 1) % 8);
			} else {
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 0;
			}
			ret = jtag_queue_transaction(tms, data, NULL, bit_len);
			if (ret < 0) {
				return ret;
			}
		} else {
			/* Enqueue the bypass IR write */
			if (jtag_queue.ir_len > (sizeof(tdi) * 8)) {
				E("IR length exceeds buffer size");
				return -EINVAL;
			}
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit IR write */
				tms[0] = 1 << ((jtag_queue.ir_len - 1) % 8);
			} else {
				tms[0] = 0;
			}
			tdi = (1 << jtag_queue.ir_len) - 1;
			ret = jtag_queue_transaction(tms, &tdi, NULL, jtag_queue.ir_len);
			if (ret < 0) {
				return ret;
			}
		}
	}
	tms[0] = 0x1;

	return jtag_queue_transaction(tms, NULL, NULL, return_to_idle ? 2 : 1);
}

static int jtag_enqueue_write_dr(const uint8_t *data, int tap_idx, int bit_len, bool return_to_idle)
{
	int ret;
	uint8_t tms[DIV_ROUND_UP(bit_len, 8)];
	uint8_t tdi;

	/* Enqueue transation into SHIFT-DR state. Assume we start in run/test idle */
	tms[0] = 0x1;
	ret = jtag_queue_transaction(tms, NULL, NULL, 3);
	if (ret < 0) {
		return ret;
	}
	/* Now, enqueue DR writes for the TAPs in bypass before this one */
	memset(tms, 0, sizeof(tms));
	for (int i = 0; i < jtag_queue.tap_count; i++) {
		if (i == tap_idx) {
			/* Enqueue the DR write */
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit DR shift */
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 1 << ((bit_len - 1) % 8);
			} else {
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 0;
			}
			ret = jtag_queue_transaction(tms, data, NULL, bit_len);
			if (ret < 0) {
				return ret;
			}
		} else {
			/* Enqueue the bypass DR write */
			if (jtag_queue.bypass_dr_len > (sizeof(tdi) * 8)) {
				E("DR length exceeds buffer size");
				return -EINVAL;
			}
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit DR shift */
				tms[0] = 1 << ((jtag_queue.bypass_dr_len - 1) % 8);
			} else {
				tms[0] = 0;
			}
			tdi = (1 << jtag_queue.bypass_dr_len) - 1;
			ret = jtag_queue_transaction(tms, &tdi, NULL, jtag_queue.bypass_dr_len);
			if (ret < 0) {
				return ret;
			}
		}
	}
	tms[0] = 0x1;

	return jtag_queue_transaction(tms, NULL, NULL, return_to_idle ? 2 : 1);
}

static int jtag_enqueue_read_dr(uint8_t *data, int tap_idx, int bit_len, bool return_to_idle)
{
	int ret;
	uint8_t tms[DIV_ROUND_UP(bit_len, 8)];
	uint8_t tdi;

	/* Enqueue transation into SHIFT-DR state. Assume we start in run/test idle */
	tms[0] = 0x1;
	ret = jtag_queue_transaction(tms, NULL, NULL, 3);
	if (ret < 0) {
		return ret;
	}
	memset(tms, 0, sizeof(tms));
	/* Now, enqueue IR writes for the TAPs in bypass before this one */
	for (int i = 0; i < jtag_queue.tap_count; i++) {
		if (i == tap_idx) {
			/* Enqueue the DR write */
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit DR shift */
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 1 << ((bit_len - 1) % 8);
			} else {
				tms[DIV_ROUND_UP(bit_len, 8) - 1] = 0;
			}
			ret = jtag_queue_transaction(tms, NULL, data, bit_len);
			if (ret < 0) {
				return ret;
			}
		} else {
			/* Enqueue the bypass DR write */
			if (jtag_queue.bypass_dr_len > (sizeof(tdi) * 8)) {
				E("DR length exceeds buffer size");
				return -EINVAL;
			}
			if (i == jtag_queue.tap_count - 1) {
				/* Set high bit of TMS to exit DR shift */
				tms[0] = 1 << ((jtag_queue.bypass_dr_len - 1) % 8);
			} else {
				tms[0] = 0;
			}
			tdi = (1 << jtag_queue.bypass_dr_len) - 1;
			ret = jtag_queue_transaction(tms, &tdi, NULL, jtag_queue.bypass_dr_len);
			if (ret < 0) {
				return ret;
			}
		}
	}
	tms[0] = 0x1;

	return jtag_queue_transaction(tms, NULL, NULL, return_to_idle ? 2 : 1);
}

static int jtag_execute_queue(void)
{
	int ret;
	int bit_idx = 0;
	struct tdo_buffer *tdo_buf = jtag_queue.tdo_buf_list;
	struct tdo_buffer *next_buf;

	/* Execute the queued transactions */
	ret = jaylink_jtag_io(devh, jtag_queue.tms, jtag_queue.tdi,
			      jtag_queue.tdo, jtag_queue.queue_idx,
			      JAYLINK_JTAG_VERSION_3);
	if (ret != JAYLINK_OK) {
		E("Failed to execute JTAG queue: %s", jaylink_strerror(ret));
		return ret;
	}
	/* Copy out TDO data */
	while (tdo_buf) {
		if (tdo_buf->buf) {
			bitcopy(tdo_buf->buf, jtag_queue.tdo, 0, bit_idx, tdo_buf->bit_len);
		}
		bit_idx += tdo_buf->bit_len;
		next_buf = tdo_buf->next;
		free(tdo_buf);
		tdo_buf = next_buf;
	}
	if (bit_idx != jtag_queue.queue_idx) {
		E("TDO data length mismatch: expected %d, got %d", jtag_queue.queue_idx, bit_idx);
		return -EINVAL;
	}
	/* Reset queue state */
	jtag_queue.queue_idx = 0;
	jtag_queue.tdo_buf_list = NULL;
	return 0;
}

static int jtag_go_idle(void)
{
	uint8_t tms = 0x1f;
	int ret;

	/* Move to run/test idle state */
	ret = jtag_queue_transaction(&tms, NULL, NULL, 6);
	if (ret != 0) {
		return ret;
	}
	ret = jtag_execute_queue();
	if (ret != 0) {
		return ret;
	}

	return 0;
}

static void arc_jtag_queue_init(void)
{
	/*
	 * In the future, we could replace this with a function that
	 * detects the TAPs in the chain and sets the bypass length
	 * accordingly. For now, we hardcode the values for the ARC TAPs.
	 */
	jtag_queue.ir_len = 4;
	jtag_queue.bypass_dr_len = 1;
	jtag_queue.tap_count = 5;
	jtag_queue.queue_idx = 0;
	jtag_queue.tdo_buf_list = NULL;
	memset(jtag_queue.tms, 0, sizeof(jtag_queue.tms));
	memset(jtag_queue.tdi, 0, sizeof(jtag_queue.tdi));
	memset(jtag_queue.tdo, 0, sizeof(jtag_queue.tdo));
}

/*
 * Reads IDCODE register from TAP 0 for attached device
 */
static int arc_jtag_read_idcode(uint32_t *idcode)
{
	int ret;
	uint8_t data;

	data = 0xc;
	/* Write IR to read IDCode */
	ret = jtag_enqueue_write_ir(&data, 0, 4, true);
	if (ret < 0) {
		E("Error, failed to queue IR write for IDCODE\n");
		return ret;
	}
	/* Read IDCode */
	ret = jtag_enqueue_read_dr((uint8_t *)idcode, 0, 32, true);
	if (ret < 0) {
		E("Error, failed to queue DR read for IDCODE\n");
		return ret;
	}
	ret = jtag_execute_queue();
	if (ret < 0) {
		E("Error, failed to execute JTAG queue\n");
		return ret;
	}
	return ret;
}

/*
 * Reads `len` bytes of device memory, starting at address `start_addr` into
 * `buf`. This function is specific to the JTAG implementation on ARC HS4x.
 */
int arc_jtag_read_mem(uint32_t start_addr, uint8_t *buf, size_t len)
{
	int ret;
	uint8_t data;
	uint32_t aligned_addr = start_addr & ~0x3;
	uint32_t unaligned_start_cnt = (start_addr & 0x3) ? 4 - (start_addr & 0x3) : 0;
	uint32_t aligned_cnt = (len - unaligned_start_cnt) & ~0x3;
	uint32_t unaligned_end_cnt = len - (aligned_cnt + unaligned_start_cnt);
	uint8_t temp[4];
	size_t cpy_idx = 0;

	/* Write to IR to select transaction CMD on TAP 1 */
	data = ARC_TRANSACTION_CMD_REG;
	ret = jtag_enqueue_write_ir(&data, 1, 4, false);
	if (ret < 0) {
		E("Error, failed to queue IR write for transaction CMD\n");
		return ret;
	}
	/* Select memory read transaction */
	data = ARC_TRANSACTION_READ_MEM;
	ret = jtag_enqueue_write_dr(&data, 1, 4, false);
	if (ret < 0) {
		E("Error, failed to queue DR write for transaction CMD\n");
		return ret;
	}
	/* Write to IR to select address on TAP 1 */
	data = ARC_ADDRESS_REG;
	ret = jtag_enqueue_write_ir(&data, 1, 4, false);
	if (ret < 0) {
		E("Error, failed to queue IR write for address CMD\n");
		return ret;
	}
	/* Write DR with address we want to access. Return to idle so transaction runs */
	ret = jtag_enqueue_write_dr((uint8_t *)&aligned_addr, 1, 32, true);
	if (ret < 0) {
		E("Error, failed to queue DR write for address CMD\n");
		return ret;
	}
	/* Write to IR to select data reg */
	data = ARC_DATA_REG;
	ret = jtag_enqueue_write_ir(&data, 1, 4, true);
	if (ret < 0) {
		E("Error, failed to queue IR write for data CMD\n");
		return ret;
	}
	if (unaligned_start_cnt) {
		/* Read the first word, but only the last `unaligned_start_cnt` bytes */
		ret = jtag_enqueue_read_dr(temp, 1, 32, true);
		if (ret < 0) {
			E("Error, failed to queue DR read for data CMD\n");
			return ret;
		}
		ret = jtag_execute_queue();
		if (ret < 0) {
			E("Error, failed to execute JTAG queue\n");
			return ret;
		}
		memcpy(&buf[cpy_idx], &temp[4 - unaligned_start_cnt], unaligned_start_cnt);
		cpy_idx += unaligned_start_cnt;
	}
	/* ARC supports automatic address increment, so just keep executing DR reads */
	for (; cpy_idx < (unaligned_start_cnt + aligned_cnt); cpy_idx += 4) {
		ret = jtag_enqueue_read_dr(&buf[cpy_idx], 1, 32, true);
		if (ret < 0) {
			E("Error, failed to queue DR read for data CMD\n");
			return ret;
		}
		ret = jtag_execute_queue();
		if (ret < 0) {
			E("Error, failed to execute JTAG queue\n");
			return ret;
		}
	}
	if (unaligned_end_cnt) {
		/* Read the last word, but only the first `unaligend_end_cnt` bytes */
		ret = jtag_enqueue_read_dr(temp, 1, 32, true);
		if (ret < 0) {
			E("Error, failed to queue DR read for data CMD\n");
			return ret;
		}
		ret = jtag_execute_queue();
		if (ret < 0) {
			E("Error, failed to execute JTAG queue\n");
			return ret;
		}
		memcpy(&buf[cpy_idx], temp, unaligned_end_cnt);
	}
	return 0;
}

/*
 * Writes `len` bytes of device memory, starting at address `start_addr` from
 * `buf`. This function is specific to the JTAG implementation on ARC HS4x.
 */
int arc_jtag_write_mem(uint32_t start_addr, const uint8_t *buf, size_t len)
{
	int ret;
	uint8_t data;
	uint32_t aligned_addr = start_addr & ~0x3;
	uint32_t unaligned_start_cnt = (start_addr & 0x3) ? 4 - (start_addr & 0x3) : 0;
	uint32_t aligned_cnt = (len - unaligned_start_cnt) & ~0x3;
	uint32_t unaligned_end_cnt = len - (aligned_cnt + unaligned_start_cnt);
	uint8_t temp[4];
	size_t cpy_idx = 0;

	if (unaligned_start_cnt) {
		/* Use a read-modify-write algorithm to set the first word */
		ret = arc_jtag_read_mem(aligned_addr, temp, 4);
		if (ret < 0) {
			E("Error, failed to read memory for unaligned write\n");
			return ret;
		}
		memcpy(&temp[4 - unaligned_start_cnt], &buf[cpy_idx], unaligned_start_cnt);
		ret = arc_jtag_write_mem(aligned_addr, temp, 4);
		if (ret < 0) {
			E("Error, failed to write memory for unaligned write\n");
			return ret;
		}
		aligned_addr += 4;
		cpy_idx += unaligned_start_cnt;
	}

	if (aligned_cnt) {
		/* Write to IR to select transaction CMD on TAP 1 */
		data = ARC_TRANSACTION_CMD_REG;
		ret = jtag_enqueue_write_ir(&data, 1, 4, false);
		if (ret < 0) {
			E("Error, failed to queue IR write for transaction CMD\n");
			return ret;
		}
		/* Select memory write transaction */
		data = ARC_TRANSACTION_WRITE_MEM;
		ret = jtag_enqueue_write_dr(&data, 1, 4, false);
		if (ret < 0) {
			E("Error, failed to queue DR write for transaction CMD\n");
			return ret;
		}
		/* Write to IR to select address register on TAP 1 */
		data = ARC_ADDRESS_REG;
		ret = jtag_enqueue_write_ir(&data, 1, 4, false);
		if (ret < 0) {
			E("Error, failed to queue IR write for address CMD\n");
			return ret;
		}
		/* Write DR with address we want to access. */
		ret = jtag_enqueue_write_dr((uint8_t *)&aligned_addr, 1, 32, false);
		if (ret < 0) {
			E("Error, failed to queue DR write for address CMD\n");
			return ret;
		}
		/* Write to IR to select data reg */
		data = ARC_DATA_REG;
		ret = jtag_enqueue_write_ir(&data, 1, 4, false);
		if (ret < 0) {
			E("Error, failed to queue IR write for data CMD\n");
			return ret;
		}
	}
	/* ARC supports automatic address increment, so just keep executing DR writes */
	for (; cpy_idx < (unaligned_start_cnt + aligned_cnt); cpy_idx += 4) {
		/* Return to idle here so we run transaction */
		ret = jtag_enqueue_write_dr(&buf[cpy_idx], 1, 32, true);
		if (ret < 0) {
			E("Error, failed to queue DR write for data CMD\n");
			return ret;
		}
		ret = jtag_execute_queue();
		if (ret < 0) {
			E("Error, failed to execute JTAG queue\n");
			return ret;
		}
	}
	if (unaligned_end_cnt) {
		/* Write the last `unaligned_end_cnt` bytes using read-modify-write */
		ret = arc_jtag_read_mem(aligned_addr + aligned_cnt, temp, 4);
		if (ret < 0) {
			E("Error, failed to read memory for unaligned write\n");
			return ret;
		}
		memcpy(temp, &buf[cpy_idx], unaligned_end_cnt);
		ret = arc_jtag_write_mem(aligned_addr + aligned_cnt, temp, 4);
		if (ret < 0) {
			E("Error, failed to write memory for unaligned write\n");
			return ret;
		}
	}
	return 0;
}

int arc_jtag_init(void *data)
{
	size_t num_devs;
	struct jaylink_device **devs;
	struct jaylink_device *selected_device = NULL;
	struct jaylink_connection conns[JAYLINK_MAX_CONNECTIONS];
	struct jtag_init_data *init_data = (struct jtag_init_data *)data;
	bool found_handle;
	size_t conn_count;
	int ret;
	uint32_t idcode;

	verbose = init_data->verbose;

	ret = jaylink_init(&ctx);
	if (ret != JAYLINK_OK) {
		return -1;
	}

	ret = jaylink_discovery_scan(ctx, JAYLINK_HIF_USB);
	if (ret != JAYLINK_OK) {
		jaylink_exit(ctx);
		ctx = NULL;
		return -1;
	}

	ret = jaylink_get_devices(ctx, &devs, &num_devs);
	if (ret != JAYLINK_OK) {
		jaylink_exit(ctx);
		ctx = NULL;
		return -1;
	}

	if (init_data->serial_number) {
		uint32_t serial, test_serial;

		ret = jaylink_parse_serial_number(init_data->serial_number, &serial);
		if (ret != JAYLINK_OK) {
			E("Invalid serial number: %s", init_data->serial_number);
			goto error;
		}
		for (size_t i = 0; i < num_devs; i++) {
			ret = jaylink_device_get_serial_number(devs[i], &test_serial);
			if (ret != JAYLINK_OK) {
				E("Failed to get serial number for device %zu", i);
				goto error;
			}
			if (test_serial == serial) {
				selected_device = devs[i];
				I("Found JLink device with serial number: %s",
					init_data->serial_number);
				break;
			}
		}
		if (selected_device == NULL) {
			E("No JLink device found with serial number: %s", init_data->serial_number);
			goto error;
		}
	} else if (num_devs > 1) {
		I("Multiple JLink devices found, using the first one.");
		selected_device = devs[0];
	} else if (num_devs == 1) {
		selected_device = devs[0];
	} else {
		E("No JLink devices found.");
		goto error;
	}
	ret = jaylink_open(selected_device, &devh);
	if (ret != JAYLINK_OK) {
		E("Failed to open JLink device: %s", jaylink_strerror(ret));
		goto error;
	}

	memset(caps, 0, JAYLINK_DEV_EXT_CAPS_SIZE);
	ret = jaylink_get_caps(devh, caps);
	if (ret != JAYLINK_OK) {
		E("Failed to get JLink capabilities: %s", jaylink_strerror(ret));
		goto error;
	}
	if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_GET_EXT_CAPS)) {
		ret = jaylink_get_extended_caps(devh, caps);
		if (ret != JAYLINK_OK) {
			E("Failed to get extended capabilities: %s", jaylink_strerror(ret));
			goto error;
		}
	}

	if (verbose > 3) {
		jaylink_log_set_level(ctx, JAYLINK_LOG_LEVEL_DEBUG);
	}

	if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_REGISTER)) {
		conn.handle = 0;
		conn.pid = 0;
		strcpy(conn.hid, "0.0.0.0");
		conn.iid = 0;
		conn.cid = 0;

		ret = jaylink_register(devh, &conn, conns, &conn_count);
		if (ret != JAYLINK_OK) {
			E("jaylink_register() failed: %s", jaylink_strerror(ret));
			goto error;
		}

		found_handle = false;

		for (size_t i = 0; i < conn_count; i++) {
			if (conns[i].handle == conn.handle) {
				found_handle = true;
				break;
			}
		}
		if (!found_handle) {
			E("Maximum number of JLink connections reached");
			ret = -ENODEV;
			goto error;
		}
	}

	/* Select JTAG interface */
	if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_SELECT_TIF)) {
		ret = jaylink_select_interface(devh, JAYLINK_TIF_JTAG, NULL);
		if (ret != JAYLINK_OK) {
			E("jaylink_select_interface() failed: %s", jaylink_strerror(ret));
			goto error;
		}
	}

	/* Init JTAG queue and check the IDCODE register */
	arc_jtag_queue_init();
	ret = jtag_go_idle();
	if (ret < 0) {
		E("Failed to enter run/test idle");
		goto error;
	}
	ret = arc_jtag_read_idcode(&idcode);
	if (ret < 0) {
		E("Failed to read IDCODE");
		goto error;
	}
	D(1, "IDCODE: 0x%08X", idcode);
	if (idcode != ARC_IDCODE) {
		E("IDCODE mismatch: expected 0x%08X, got 0x%08X", ARC_IDCODE, idcode);
		ret = -ENODEV;
		goto error;
	}

	jaylink_free_devices(devs, true);
	return 0;
error:
	if (devh) {
		jaylink_close(devh);
		devh = NULL;
	}
	jaylink_free_devices(devs, true);
	jaylink_exit(ctx);
	ctx = NULL;
	return ret;
}

void arc_jtag_exit(void)
{
	size_t conn_count;
	struct jaylink_connection conns[JAYLINK_MAX_CONNECTIONS];

	if (devh) {
		if (jaylink_has_cap(caps, JAYLINK_DEV_CAP_REGISTER)) {
			jaylink_unregister(devh, &conn, conns, &conn_count);
		}
		jaylink_close(devh);
		devh = NULL;
	}
	if (ctx) {
		jaylink_exit(ctx);
		ctx = NULL;
	}
}
