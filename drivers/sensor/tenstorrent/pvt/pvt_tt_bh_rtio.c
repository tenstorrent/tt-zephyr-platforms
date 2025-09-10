/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor/tenstorrent/pvt_tt_bh.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/rtio/work.h>

LOG_MODULE_DECLARE(pvt_tt_bh);

#define SDIF_DONE_TIMEOUT_MS          10
#define TS_PD_OFFSET                  0x40
#define VM_OFFSET                     0x200
#define GET_TS_REG_ADDR(ID, REG_NAME) (ID * TS_PD_OFFSET + PVT_CNTL_TS_00_##REG_NAME##_REG_ADDR)
#define GET_PD_REG_ADDR(ID, REG_NAME) (ID * TS_PD_OFFSET + PVT_CNTL_PD_00_##REG_NAME##_REG_ADDR)
#define GET_VM_REG_ADDR(ID, REG_NAME) (ID * VM_OFFSET + PVT_CNTL_VM_00_##REG_NAME##_REG_ADDR)

/* Delay Chain / Oscillator definitions */
#define ALL_AGING_OSC 0x7 /* enable delay chain 19, 20, 21 for aging measurement */
#define IP_CFG0_ADDR  0x1
#define IP_CNTL_ADDR  0x0

#define PVT_CNTL_IRQ_EN_REG_ADDR             0x80080040
#define PVT_CNTL_TS_00_IRQ_ENABLE_REG_ADDR   0x800800C0
#define PVT_CNTL_PD_00_IRQ_ENABLE_REG_ADDR   0x80080340
#define PVT_CNTL_VM_00_IRQ_ENABLE_REG_ADDR   0x80080A00
#define PVT_CNTL_TS_00_ALARMA_CFG_REG_ADDR   0x800800E0
#define PVT_CNTL_TS_00_ALARMB_CFG_REG_ADDR   0x800800E4
#define PVT_CNTL_TS_CMN_CLK_SYNTH_REG_ADDR   0x80080080
#define PVT_CNTL_PD_CMN_CLK_SYNTH_REG_ADDR   0x80080300
#define PVT_CNTL_VM_CMN_CLK_SYNTH_REG_ADDR   0x80080800
#define PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR 0x80080308
#define PVT_CNTL_PD_CMN_SDIF_REG_ADDR        0x8008030C
#define PVT_CNTL_TS_CMN_SDIF_STATUS_REG_ADDR 0x80080088
#define PVT_CNTL_TS_CMN_SDIF_REG_ADDR        0x8008008C
#define PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR 0x80080308
#define PVT_CNTL_PD_CMN_SDIF_REG_ADDR        0x8008030C
#define PVT_CNTL_VM_CMN_SDIF_STATUS_REG_ADDR 0x80080808
#define PVT_CNTL_VM_CMN_SDIF_REG_ADDR        0x8008080C
#define PVT_CNTL_TS_00_SDIF_DONE_REG_ADDR    0x800800D4
#define PVT_CNTL_TS_00_SDIF_DATA_REG_ADDR    0x800800D8
#define PVT_CNTL_VM_00_SDIF_RDATA_REG_ADDR   0x80080A30
#define PVT_CNTL_PD_00_SDIF_DONE_REG_ADDR    0x80080354
#define PVT_CNTL_PD_00_SDIF_DATA_REG_ADDR    0x80080358

typedef struct {
	uint32_t ip_dat: 16;
	uint32_t ip_type: 1;
	uint32_t ip_fault: 1;
	uint32_t ip_done: 1;
	uint32_t reserved: 1;
	uint32_t ip_ch: 4;
} ip_data_reg_t;

typedef union {
	uint32_t val;
	ip_data_reg_t f;
} ip_data_reg_u;

typedef struct {
	uint32_t sample_data: 16;
	uint32_t sample_type: 1;
	uint32_t sample_fault: 1;
} pvt_cntl_ts_pd_sdif_data_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_ts_pd_sdif_data_reg_t f;
} pvt_cntl_ts_pd_sdif_data_reg_u;

typedef struct {
	uint32_t run_mode: 4;
	uint32_t reserved_1: 4;
	uint32_t oscillator_select: 5;
	uint32_t oscillator_enable: 3;
	uint32_t counter_divide_ratio: 2;
	uint32_t reserved_2: 2;
	uint32_t counter_gate: 2;
	uint32_t reserved_3: 10;
} pd_ip_cfg0_t;

typedef union {
	uint32_t val;
	pd_ip_cfg0_t f;
} pd_ip_cfg0_u;

typedef struct {
	uint32_t sdif_wdata: 24;
	uint32_t sdif_addr: 3;
	uint32_t sdif_wrn: 1;
	uint32_t rsvd_0: 3;
	uint32_t sdif_prog: 1;
} pvt_cntl_sdif_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_sdif_reg_t f;
} pvt_cntl_sdif_reg_u;

typedef struct {
	uint32_t sdif_busy: 1;
	uint32_t sdif_lock: 1;
} pvt_cntl_sdif_status_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_sdif_status_reg_t f;
} pvt_cntl_sdif_status_reg_u;

typedef enum {
	TS = 0,
	PD = 1,
	VM = 2,
} PvtType;

static uint32_t selected_pd_delay_chain = 0xFF; /* Invalid initial value */
static uint32_t new_delay_chain = 1;

static void wait_sdif_ready(uint32_t status_reg_addr)
{
	pvt_cntl_sdif_status_reg_u sdif_status;

	do {
		sdif_status.val = sys_read32(status_reg_addr);
	} while (sdif_status.f.sdif_busy == 1);
}

static void sdif_write(uint32_t status_reg_addr, uint32_t wr_data_reg_addr, uint32_t sdif_addr,
		       uint32_t data)
{
	wait_sdif_ready(status_reg_addr);
	pvt_cntl_sdif_reg_u sdif;

	sdif.val = 0;
	sdif.f.sdif_addr = sdif_addr;
	sdif.f.sdif_wdata = data;
	sdif.f.sdif_wrn = 1;
	sdif.f.sdif_prog = 1;
	sys_write32(sdif.val, wr_data_reg_addr);
}

static void select_delay_chain_and_start_pd_conv(uint32_t delay_chain)
{
	if (delay_chain != selected_pd_delay_chain) {
		pd_ip_cfg0_u ip_cfg0;

		ip_cfg0.val = 0;
		ip_cfg0.f.run_mode = 0; /* MODE_PD_CNV */
		ip_cfg0.f.oscillator_enable = ALL_AGING_OSC;
		ip_cfg0.f.oscillator_select = delay_chain;
		ip_cfg0.f.counter_gate = 0x3; /* W = 255 */

		sdif_write(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR,
			   IP_CFG0_ADDR, ip_cfg0.val);
		sdif_write(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR,
			   IP_CNTL_ADDR, 0x108);

		/* wait until delay chain takes effect */
		k_usleep(250);
		selected_pd_delay_chain = delay_chain;
	}
}

typedef enum {
	ValidData = 0,
	AnalogueAccess = 1,
} SampleType;

static uint32_t get_pvt_addr(PvtType type, uint32_t id, uint32_t base_addr)
{
	uint32_t offset;

	if (type == VM) {
		offset = VM_OFFSET;
	} else {
		offset = TS_PD_OFFSET;
	}
	return id * offset + base_addr;
}

static ReadStatus read_pvt_auto_mode(PvtType type, uint32_t id, uint16_t *data,
				     uint32_t sdif_done_base_addr, uint32_t sdif_data_base_addr)
{
	if (!data) {
		return SampleFault;
	}

	uint32_t sdif_done;
	uint64_t deadline = k_uptime_get() + SDIF_DONE_TIMEOUT_MS;
	bool timeout = false;

	do {
		sdif_done = sys_read32(get_pvt_addr(type, id, sdif_done_base_addr));
		timeout = k_uptime_get() > deadline;
	} while (!sdif_done && !timeout);

	if (timeout) {
		return SdifTimeout;
	}

	pvt_cntl_ts_pd_sdif_data_reg_u ts_sdif_data;

	ts_sdif_data.val = sys_read32(get_pvt_addr(type, id, sdif_data_base_addr));

	if (ts_sdif_data.f.sample_fault) {
		return SampleFault;
	}
	if (ts_sdif_data.f.sample_type != ValidData) {
		return IncorrectSampleType;
	}
	*data = ts_sdif_data.f.sample_data;
	return ReadOk;
}

static ReadStatus read_ts(uint32_t id, uint16_t *data)
{
	return read_pvt_auto_mode(TS, id, data, PVT_CNTL_TS_00_SDIF_DONE_REG_ADDR,
				  PVT_CNTL_TS_00_SDIF_DATA_REG_ADDR);
}

static ReadStatus read_ts_avg(uint16_t *sum)
{
	*sum = 0;
	for (int i = 0; i < 8; i++) {
		uint16_t data;

		read_pvt_auto_mode(TS, i, &data, PVT_CNTL_TS_00_SDIF_DONE_REG_ADDR,
				   PVT_CNTL_TS_00_SDIF_DATA_REG_ADDR);
		*sum += data;
	}
	*sum /= 8;
	return ReadOk;
}

/* can not readback supply check in auto mode, use manual read instead */
static ReadStatus read_vm(uint32_t id, uint16_t *data)
{
	if (!data) {
		return SampleFault;
	}

	/* ignore ip_done in auto_mode */
	ip_data_reg_u ip_data;

	ip_data.val = sys_read32(GET_VM_REG_ADDR(id, SDIF_RDATA));

	if (ip_data.f.ip_fault) {
		return SampleFault;
	}
	if (ip_data.f.ip_type != ValidData) {
		return IncorrectSampleType;
	}
	*data = ip_data.f.ip_dat;
	return ReadOk;
}

static ReadStatus read_pd(uint32_t id, uint32_t delay_chain, uint16_t *data)
{
	select_delay_chain_and_start_pd_conv(delay_chain);

	return read_pvt_auto_mode(PD, id, data, PVT_CNTL_PD_00_SDIF_DONE_REG_ADDR,
				  PVT_CNTL_PD_00_SDIF_DATA_REG_ADDR);
}

static void pvt_tt_bh_submit_sample(struct rtio_iodev_sqe *iodev_sqe)
{
	const struct sensor_read_config *sensor_cfg =
		(const struct sensor_read_config *)iodev_sqe->sqe.iodev->data;
	uint32_t min_buffer_len = sizeof(struct pvt_tt_bh_rtio_data) * sensor_cfg->count;
	uint8_t *buf;
	uint32_t buf_len;
	int ret;

	/* Get RTIO output buffer. */
	ret = rtio_sqe_rx_buf(iodev_sqe, min_buffer_len, min_buffer_len, &buf, &buf_len);
	if (ret != 0) {
		LOG_ERR("Failed to get a read buffer of size %u bytes", min_buffer_len);
		rtio_iodev_sqe_err(iodev_sqe, ret);
		return;
	}

	/* struct pvt_tt_bh_rtio_data *data = (struct pvt_tt_bh_rtio_data *)buf; */
	struct pvt_tt_bh_rtio_data *data = (struct pvt_tt_bh_rtio_data *)buf;
	ReadStatus status;

	const struct pvt_tt_bh_config *pvt_cfg =
		(const struct pvt_tt_bh_config *)sensor_cfg->sensor->config;

	for (size_t i = 0; i < sensor_cfg->count; i++) {
		const struct sensor_chan_spec *chan = &sensor_cfg->channels[i];

		data[i].spec = *chan;

		/* Validate channel index bounds and read data */
		switch (chan->chan_type) {
		case SENSOR_CHAN_PVT_TT_BH_PD:
			if (chan->chan_idx >= pvt_cfg->num_pd) {
				LOG_ERR("Invalid channel index %d out of %d sensors",
					chan->chan_idx, pvt_cfg->num_pd);
				rtio_iodev_sqe_err(iodev_sqe, -EINVAL);
				return;
			}
			status = read_pd(chan->chan_idx, new_delay_chain, &data[i].raw);
			break;
		case SENSOR_CHAN_PVT_TT_BH_VM:
			if (chan->chan_idx >= pvt_cfg->num_vm) {
				LOG_ERR("Invalid channel index %d out of %d sensors",
					chan->chan_idx, pvt_cfg->num_vm);
				rtio_iodev_sqe_err(iodev_sqe, -EINVAL);
				return;
			}
			status = read_vm(chan->chan_idx, &data[i].raw);
			break;
		case SENSOR_CHAN_PVT_TT_BH_TS:
			if (chan->chan_idx >= pvt_cfg->num_ts) {
				LOG_ERR("Invalid channel index %d out of %d sensors",
					chan->chan_idx, pvt_cfg->num_ts);
				rtio_iodev_sqe_err(iodev_sqe, -EINVAL);
				return;
			}
			status = read_ts(chan->chan_idx, &data[i].raw);
			break;
		case SENSOR_CHAN_PVT_TT_BH_TS_AVG:
			/* Channel index is ignored as this is the average for all TS channels. */
			status = read_ts_avg(&data[i].raw);
			break;
		default:
			LOG_ERR("Unsupported channel type: %d", chan->chan_type);
			rtio_iodev_sqe_err(iodev_sqe, -ENOTSUP);
			return;
		}

		if (status != ReadOk) {
			LOG_ERR("Failed to read data %d", status);
			rtio_iodev_sqe_err(iodev_sqe, status);
			return;
		}
	}

	rtio_iodev_sqe_ok(iodev_sqe, 0);
}

void pvt_tt_bh_submit(const struct device *sensor, struct rtio_iodev_sqe *sqe)
{
	const struct rtio_sqe *event = &sqe->sqe;

	if (!event->iodev) {
		LOG_ERR("IO device is null");
		rtio_iodev_sqe_err(sqe, -EINVAL);
		return;
	}

	if (event->op != RTIO_OP_RX) {
		LOG_ERR("Sensor submit expects the RX opcode");
		rtio_iodev_sqe_err(sqe, -EINVAL);
		return;
	}

	struct rtio_work_req *req = rtio_work_req_alloc();

	rtio_work_req_submit(req, sqe, pvt_tt_bh_submit_sample);
}

void pvt_tt_bh_delay_chain_set(uint32_t new_delay_chain_)
{
	new_delay_chain = new_delay_chain_;
}
