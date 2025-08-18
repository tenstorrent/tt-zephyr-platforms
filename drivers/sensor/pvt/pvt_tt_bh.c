/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT tenstorrent_bh_pvt

#include "reg.h"
#include "tenstorrent/msg_type.h"
#include "tenstorrent/msgqueue.h"
#include "tenstorrent/post_code.h"

#include <float.h> /* for FLT_MAX */
#include <math.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/clock_control_tt_bh.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pvt/pvt_tt_bh.h>
#include <zephyr/rtio/rtio.h>

LOG_MODULE_REGISTER(pvt_tt_bh, CONFIG_SENSOR_LOG_LEVEL);

static const struct device *const pll_dev_1 = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(pll1));

#define SDIF_DONE_TIMEOUT_MS 10
#define MIN_BUFFER_SIZE      sizeof(uint16_t)

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

/* these macros are used for registers specific for each sensor */
#define TS_PD_OFFSET                  0x40
#define VM_OFFSET                     0x200
#define GET_TS_REG_ADDR(ID, REG_NAME) (ID * TS_PD_OFFSET + PVT_CNTL_TS_00_##REG_NAME##_REG_ADDR)
#define GET_PD_REG_ADDR(ID, REG_NAME) (ID * TS_PD_OFFSET + PVT_CNTL_PD_00_##REG_NAME##_REG_ADDR)
#define GET_VM_REG_ADDR(ID, REG_NAME) (ID * VM_OFFSET + PVT_CNTL_VM_00_##REG_NAME##_REG_ADDR)

#define VM_VREF 1.2207

/* SDIF address */
#define IP_CNTL_ADDR    0x0
#define IP_CFG0_ADDR    0x1
#define IP_CFGA_ADDR    0x2
#define IP_DATA_ADDR    0x3
#define IP_POLLING_ADDR 0x4
#define IP_TMR_ADDR     0x5
#define IP_CFG1_ADDR    0x6

/* therm_trip temperature in degrees C */
#define ALARM_A_THERM_TRIP_TEMP 83
/* BH prod spec 7.3 gives Tj,shutdown=110C, tmons are +-1C calibrated */
#define ALARM_B_THERM_TRIP_TEMP 109

#define TS_HYSTERESIS_DELTA 5

#define ALL_AGING_OSC 0x7 /* enable delay chain 19, 20, 21 for aging measurement */

#define NUM_TS 8
#define NUM_VM 8
#define NUM_PD 16

typedef enum {
	TS = 0,
	PD = 1,
	VM = 2,
} PvtType;

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
	uint32_t run_mode: 4;
	uint32_t reserved_0: 1;
	uint32_t resolution: 2;
	uint32_t reserved_1: 25;
} ts_ip_cfg0_t;

typedef union {
	uint8_t val;
	ts_ip_cfg0_t f;
} ts_ip_cfg0_u;

typedef enum {
	ValidData = 0,
	AnalogueAccess = 1,
} SampleType;

typedef struct {
	uint32_t tmr_irq_enable: 1;
	uint32_t ts_irq_enable: 1;
	uint32_t vm_irq_enable: 1;
	uint32_t pd_irq_enable: 1;
} pvt_cntl_irq_en_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_irq_en_reg_t f;
} pvt_cntl_irq_en_reg_u;

#define PVT_CNTL_IRQ_EN_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t irq_en_fault: 1;
	uint32_t irq_en_done: 1;
	uint32_t rsvd_0: 1;
	uint32_t irq_en_alarm_a: 1;
	uint32_t irq_en_alarm_b: 1;
} pvt_cntl_ts_pd_irq_enable_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_ts_pd_irq_enable_reg_t f;
} pvt_cntl_ts_pd_irq_enable_reg_u;

#define PVT_CNTL_TS_PD_IRQ_ENABLE_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t irq_en_fault: 1;
	uint32_t irq_en_done: 1;
} pvt_cntl_vm_irq_enable_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_vm_irq_enable_reg_t f;
} pvt_cntl_vm_irq_enable_reg_u;

#define PVT_CNTL_VM_IRQ_ENABLE_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t hyst_thresh: 16;
	uint32_t alarm_thresh: 16;
} pvt_cntl_vm_alarma_cfg_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_vm_alarma_cfg_reg_t f;
} pvt_cntl_vm_alarma_cfg_reg_u;

#define PVT_CNTL_VM_ALARMA_CFG_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t hyst_thresh: 16;
	uint32_t alarm_thresh: 16;
} pvt_cntl_vm_alarmb_cfg_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_vm_alarmb_cfg_reg_t f;
} pvt_cntl_vm_alarmb_cfg_reg_u;

#define PVT_CNTL_VM_ALARMB_CFG_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t clk_synth_lo: 8;
	uint32_t clk_synth_hi: 8;
	uint32_t clk_synth_hold: 4;
	uint32_t rsvd_0: 4;
	uint32_t clk_synth_en: 1;
} pvt_cntl_clk_synth_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_clk_synth_reg_t f;
} pvt_cntl_clk_synth_reg_u;

#define PVT_CNTL_CLK_SYNTH_REG_DEFAULT (0x00010000)

typedef struct {
	uint32_t sdif_busy: 1;
	uint32_t sdif_lock: 1;
} pvt_cntl_sdif_status_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_sdif_status_reg_t f;
} pvt_cntl_sdif_status_reg_u;

#define PVT_CNTL_SDIF_STATUS_REG_DEFAULT (0x00000000)

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

#define PVT_CNTL_SDIF_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t sample_data: 16;
	uint32_t sample_type: 1;
	uint32_t sample_fault: 1;
} pvt_cntl_ts_pd_sdif_data_reg_t;

typedef union {
	uint32_t val;
	pvt_cntl_ts_pd_sdif_data_reg_t f;
} pvt_cntl_ts_pd_sdif_data_reg_u;

struct pvt_tt_bh_config {
	uint8_t num_pd;
	uint8_t num_vm;
	uint8_t num_ts;
};

struct pvt_tt_bh_data {
};

/* return TS temperature in C */
static float DoutToTemp(uint16_t dout)
{
	float Eqbs = dout / 4096.0 - 0.5;
	/* TODO: slope and offset need to be replaced with fused values */
	return 83.09f + 262.5f * Eqbs;
}

/* return VM voltage in V */
static float DoutToVolt(uint16_t dout)
{
	float k1 = VM_VREF * 6 / (5 * 16384);
	float offset = VM_VREF / 5 * (3 / 256 + 1);

	return k1 * dout - offset;
	/* TODO: if fused, return k3 * (N-N0) / 16384 */
}

/* return PD frequency in MHz */
static float DoutToFreq(uint16_t dout)
{
	float A = 4.0;
	float B = 1.0;
	float W = 255.0;
	float fclk = 5.0;

	return dout * A * B * fclk / W;
}

static uint16_t TempToDout(float temp)
{
	return (uint16_t)(((temp - 83.09f) / 262.5f + 0.5f) * 4096);
}

/* setup 4 sources of interrupts for each type of sensor: */
/* 1. sample done */
/* 2. alarm a: rising alarm, ignored (see section 14 of PVT controller spec) */
/* 3. alarm b: rising alarm (see section 14 of PVT controller spec) */
/* 4. IP has a fault */
/* For VM, only enable sample done and fault interrupts, as alarma and alarmb is per channel */
/* and we do not enable any channel in VM. */
static inline void pvt_tt_bh_interrupt_config(void)
{
	/* Enable Global interrupt for TS and PD */
	pvt_cntl_irq_en_reg_u irq_en;

	irq_en.val = PVT_CNTL_IRQ_EN_REG_DEFAULT;
	irq_en.f.ts_irq_enable = 1;
	irq_en.f.pd_irq_enable = 1;
	irq_en.f.vm_irq_enable = 1;
	WriteReg(PVT_CNTL_IRQ_EN_REG_ADDR, irq_en.val);

	/* Enable sources of interrupts for TS, PD, and VM */
	pvt_cntl_ts_pd_irq_enable_reg_u ts_irq_en;

	ts_irq_en.val = PVT_CNTL_TS_PD_IRQ_ENABLE_REG_DEFAULT;
	ts_irq_en.f.irq_en_alarm_a = 1;
	ts_irq_en.f.irq_en_alarm_b = 1;
	ts_irq_en.f.irq_en_done = 1;
	ts_irq_en.f.irq_en_fault = 1;
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, IRQ_ENABLE), ts_irq_en.val);
	}

	pvt_cntl_vm_irq_enable_reg_u pd_vm_irq_en;

	pd_vm_irq_en.val = PVT_CNTL_VM_IRQ_ENABLE_REG_DEFAULT;
	pd_vm_irq_en.f.irq_en_fault = 1;
	pd_vm_irq_en.f.irq_en_done = 1;
	for (uint32_t i = 0; i < NUM_PD; i++) {
		WriteReg(GET_PD_REG_ADDR(i, IRQ_ENABLE), pd_vm_irq_en.val);
	}

	for (uint32_t i = 0; i < NUM_VM; i++) {
		WriteReg(GET_VM_REG_ADDR(i, IRQ_ENABLE), pd_vm_irq_en.val);
	}

	/* Configure Alarm A */
	pvt_cntl_vm_alarma_cfg_reg_u pvt_alarma_cfg;

	pvt_alarma_cfg.val = PVT_CNTL_VM_ALARMA_CFG_REG_DEFAULT;
	pvt_alarma_cfg.f.hyst_thresh = TempToDout(ALARM_A_THERM_TRIP_TEMP - TS_HYSTERESIS_DELTA);
	pvt_alarma_cfg.f.alarm_thresh = TempToDout(ALARM_A_THERM_TRIP_TEMP);
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, ALARMA_CFG), pvt_alarma_cfg.val);
	}

	/* Configure Alarm B */
	pvt_cntl_vm_alarmb_cfg_reg_u pvt_alarmb_cfg;

	pvt_alarmb_cfg.val = PVT_CNTL_VM_ALARMB_CFG_REG_DEFAULT;
	pvt_alarmb_cfg.f.hyst_thresh = TempToDout(ALARM_B_THERM_TRIP_TEMP - TS_HYSTERESIS_DELTA);
	pvt_alarmb_cfg.f.alarm_thresh = TempToDout(ALARM_B_THERM_TRIP_TEMP);
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, ALARMB_CFG), pvt_alarmb_cfg.val);
	}
}

/* PVT clocks works in range of 4-8MHz and are derived from APB clock */
/* target a PVT clock of 5 MHz */
static inline void pvt_tt_bh_clock_config(void)
{
	uint32_t apb_clk = 0;

	clock_control_get_rate(pll_dev_1, (clock_control_subsys_t)CLOCK_CONTROL_TT_BH_CLOCK_APBCLK,
			       &apb_clk);
	pvt_cntl_clk_synth_reg_u clk_synt;

	clk_synt.val = PVT_CNTL_CLK_SYNTH_REG_DEFAULT;
	uint32_t synth = (apb_clk * 0.2 - 2) * 0.5;

	clk_synt.f.clk_synth_lo = synth;
	clk_synt.f.clk_synth_hi = synth;
	clk_synt.f.clk_synth_hold = 2;
	clk_synt.f.clk_synth_en = 1;
	WriteReg(PVT_CNTL_TS_CMN_CLK_SYNTH_REG_ADDR, clk_synt.val);
	WriteReg(PVT_CNTL_PD_CMN_CLK_SYNTH_REG_ADDR, clk_synt.val);
	WriteReg(PVT_CNTL_VM_CMN_CLK_SYNTH_REG_ADDR, clk_synt.val);
}

static void WaitSdifReady(uint32_t status_reg_addr)
{
	pvt_cntl_sdif_status_reg_u sdif_status;

	do {
		sdif_status.val = ReadReg(status_reg_addr);
	} while (sdif_status.f.sdif_busy == 1);
}

static void SdifWrite(uint32_t status_reg_addr, uint32_t wr_data_reg_addr, uint32_t sdif_addr,
		      uint32_t data)
{
	WaitSdifReady(status_reg_addr);
	pvt_cntl_sdif_reg_u sdif;

	sdif.val = PVT_CNTL_SDIF_REG_DEFAULT;
	sdif.f.sdif_addr = sdif_addr;
	sdif.f.sdif_wdata = data;
	sdif.f.sdif_wrn = 1;
	sdif.f.sdif_prog = 1;
	WriteReg(wr_data_reg_addr, sdif.val);
}

static void EnableAgingMeas(void)
{
	pd_ip_cfg0_u ip_cfg0;

	ip_cfg0.val = 0;
	ip_cfg0.f.oscillator_enable = ALL_AGING_OSC;
	SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR, IP_CFG0_ADDR,
		  ip_cfg0.val);
}

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

static ReadStatus ReadPvtAutoMode(PvtType type, uint32_t id, uint16_t *data,
				  uint32_t sdif_done_base_addr, uint32_t sdif_data_base_addr)
{
	if (!data) {
		return SampleFault;
	}

	uint32_t sdif_done;
	uint64_t deadline = k_uptime_get() + SDIF_DONE_TIMEOUT_MS;
	bool timeout = false;

	do {
		sdif_done = ReadReg(get_pvt_addr(type, id, sdif_done_base_addr));
		timeout = k_uptime_get() > deadline;
	} while (!sdif_done && !timeout);

	if (timeout) {
		return SdifTimeout;
	}

	pvt_cntl_ts_pd_sdif_data_reg_u ts_sdif_data;

	ts_sdif_data.val = ReadReg(get_pvt_addr(type, id, sdif_data_base_addr));

	if (ts_sdif_data.f.sample_fault) {
		return SampleFault;
	}
	if (ts_sdif_data.f.sample_type != ValidData) {
		return IncorrectSampleType;
	}
	*data = ts_sdif_data.f.sample_data;
	return ReadOk;
}

static ReadStatus ReadTS(uint32_t id, uint16_t *data)
{
	return ReadPvtAutoMode(TS, id, data, PVT_CNTL_TS_00_SDIF_DONE_REG_ADDR,
			       PVT_CNTL_TS_00_SDIF_DATA_REG_ADDR);
}

/* can not readback supply check in auto mode, use manual read instead */
static ReadStatus ReadVM(uint32_t id, uint16_t *data)
{
	if (!data) {
		return SampleFault;
	}

	/* ignore ip_done in auto_mode */
	ip_data_reg_u ip_data;

	ip_data.val = ReadReg(GET_VM_REG_ADDR(id, SDIF_RDATA));

	if (ip_data.f.ip_fault) {
		return SampleFault;
	}
	if (ip_data.f.ip_type != ValidData) {
		return IncorrectSampleType;
	}
	*data = ip_data.f.ip_dat;
	return ReadOk;
}

static ReadStatus ReadPD(uint32_t id, uint16_t *data)
{
	return ReadPvtAutoMode(PD, id, data, PVT_CNTL_PD_00_SDIF_DONE_REG_ADDR,
			       PVT_CNTL_PD_00_SDIF_DATA_REG_ADDR);
}

/*
 * Setup Interrupt and clk configurations, TS, PD, VM IP configurations.
 * Enable continuous mode for TS and VM. For PD, run once mode should be used.
 */
static int pvt_tt_bh_init(const struct device *dev)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP5);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Enable Process + Voltage + Thermal monitors */
	pvt_tt_bh_interrupt_config();
	pvt_tt_bh_clock_config();

	/* Configure TS */
	SdifWrite(PVT_CNTL_TS_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_TS_CMN_SDIF_REG_ADDR, IP_TMR_ADDR,
		  0x100); /* 256 cycles for TS */

	/* MODE_RUN_0, 8-bit resolution */
	ts_ip_cfg0_u ts_ip_cfg0 = {.f.run_mode = 0, .f.resolution = 2};

	SdifWrite(PVT_CNTL_TS_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_TS_CMN_SDIF_REG_ADDR, IP_CFG0_ADDR,
		  ts_ip_cfg0.val);
	SdifWrite(PVT_CNTL_TS_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_TS_CMN_SDIF_REG_ADDR, IP_CNTL_ADDR,
		  0x108); /* ip_run_cont */

	/* Configure PD */
	SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR, IP_TMR_ADDR,
		  0x0); /* 0 cycles for PD */
	SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR, IP_CNTL_ADDR,
		  0x100); /* ip_auto to release reset and pd */
	EnableAgingMeas();

	/* Configure VM */
	SdifWrite(PVT_CNTL_VM_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_VM_CMN_SDIF_REG_ADDR, IP_TMR_ADDR,
		  0x40); /* 64 cycles for VM */
	SdifWrite(PVT_CNTL_VM_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_VM_CMN_SDIF_REG_ADDR, IP_CFG0_ADDR,
		  0x1000); /* use 14-bit resolution, MODE_RUN_0, select supply check */
	SdifWrite(PVT_CNTL_VM_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_VM_CMN_SDIF_REG_ADDR, IP_CNTL_ADDR,
		  0x108); /* ip_auto to release reset and pd */

	/* Wait for all sensors to power up, TS takes 256 ip_clk cycles */
	k_usleep(100);

	return 0;
}

int pvt_tt_bh_attr_get(const struct device *dev, enum sensor_channel chan,
		       enum sensor_attribute attr, struct sensor_value *val)
{
	if (!dev || !val) {
		return -EINVAL;
	}

	const struct pvt_tt_bh_config *config = (const struct pvt_tt_bh_config *)dev->config;
	enum pvt_tt_bh_attribute pvt_attr = (enum pvt_tt_bh_attribute)attr;

	/* Initialize val to zero */
	val->val1 = 0;
	val->val2 = 0;

	switch (pvt_attr) {
	case SENSOR_ATTR_PVT_TT_BH_NUM_TS:
		val->val1 = config->num_ts;
		break;
	case SENSOR_ATTR_PVT_TT_BH_NUM_VM:
		val->val1 = config->num_vm;
		break;
	case SENSOR_ATTR_PVT_TT_BH_NUM_PD:
		val->val1 = config->num_pd;
		break;
	default:
		return -ENOTSUP;
	}
	return 0;
}

int pvt_tt_bh_decode(const uint8_t *buffer, struct sensor_chan_spec channel, uint32_t *fit,
		     uint16_t max_count, void *data_out)
{
	if (!buffer || !fit || max_count == 0 || !data_out) {
		return -EINVAL;
	}

	/* Validate channel type */
	switch (channel.chan_type) {
	case SENSOR_CHAN_PVT_TT_BH_TS:
	case SENSOR_CHAN_PVT_TT_BH_VM:
	case SENSOR_CHAN_PVT_TT_BH_PD:
		break;
	default:
		return -ENOTSUP;
	}

	float *output = (float *)data_out;
	uint16_t decoded_count = 0;
	uint16_t raw_data;

	while (*fit + sizeof(uint16_t) <= UINT32_MAX && decoded_count < max_count) {
		/* Ensure we don't read beyond available data */
		raw_data = *(uint16_t *)(buffer + *fit);
		*fit += sizeof(uint16_t);

		switch (channel.chan_type) {
		case SENSOR_CHAN_PVT_TT_BH_TS:
			output[decoded_count] = DoutToTemp(raw_data);
			break;
		case SENSOR_CHAN_PVT_TT_BH_VM:
			output[decoded_count] = DoutToVolt(raw_data);
			break;
		case SENSOR_CHAN_PVT_TT_BH_PD:
			output[decoded_count] = DoutToFreq(raw_data);
			break;
		}

		decoded_count++;

		/* For single sample reads, break after one sample */
		break;
	}

	return decoded_count;
}

static void pvt_tt_bh_submit(const struct device *sensor, struct rtio_iodev_sqe *sqe)
{
	const struct pvt_tt_bh_config *config = (const struct pvt_tt_bh_config *)sensor->config;
	const struct sensor_read_config *cfg = sqe->sqe.iodev->data;
	uint8_t *buffer;
	uint32_t buffer_len;
	int rc;

	/* Get the buffer from the submission queue entry */
	rc = rtio_sqe_rx_buf(sqe, MIN_BUFFER_SIZE, MIN_BUFFER_SIZE, &buffer, &buffer_len);
	if (rc != 0) {
		LOG_ERR("Failed to get buffer: %d", rc);
		rtio_iodev_sqe_err(sqe, rc);
		return;
	}

	/* Validate channel configuration */
	if (cfg->count != 1 || cfg->channels == NULL) {
		LOG_ERR("Invalid channel configuration");
		rtio_iodev_sqe_err(sqe, -EINVAL);
		return;
	}

	const struct sensor_chan_spec *chan = cfg->channels;
	uint16_t raw_data = 0;
	ReadStatus status = SampleFault;

	/* Validate channel index bounds */
	switch (chan->chan_type) {
	case SENSOR_CHAN_PVT_TT_BH_TS:
		if (chan->chan_idx >= config->num_ts) {
			LOG_ERR("TS channel index %d out of bounds (max: %d)", chan->chan_idx,
				config->num_ts - 1);
			rtio_iodev_sqe_err(sqe, -EINVAL);
			return;
		}
		status = ReadTS(chan->chan_idx, &raw_data);
		break;
	case SENSOR_CHAN_PVT_TT_BH_VM:
		if (chan->chan_idx >= config->num_vm) {
			LOG_ERR("VM channel index %d out of bounds (max: %d)", chan->chan_idx,
				config->num_vm - 1);
			rtio_iodev_sqe_err(sqe, -EINVAL);
			return;
		}
		status = ReadVM(chan->chan_idx, &raw_data);
		break;
	case SENSOR_CHAN_PVT_TT_BH_PD:
		if (chan->chan_idx >= config->num_pd) {
			LOG_ERR("PD channel index %d out of bounds (max: %d)", chan->chan_idx,
				config->num_pd - 1);
			rtio_iodev_sqe_err(sqe, -EINVAL);
			return;
		}
		status = ReadPD(chan->chan_idx, &raw_data);
		break;
	default:
		LOG_ERR("Unsupported channel type: %d", chan->chan_type);
		rtio_iodev_sqe_err(sqe, -ENOTSUP);
		return;
	}

	/* Handle read errors */
	if (status != ReadOk) {
		int error_code;

		switch (status) {
		case SdifTimeout:
			error_code = -ETIMEDOUT;
			LOG_ERR("SDIF timeout for channel %d", chan->chan_idx);
			break;
		case SampleFault:
			error_code = -EIO;
			LOG_ERR("Sample fault for channel %d", chan->chan_idx);
			break;
		case IncorrectSampleType:
			error_code = -EPROTO;
			LOG_ERR("Incorrect sample type for channel %d", chan->chan_idx);
			break;
		default:
			error_code = -EIO;
			LOG_ERR("Unknown error %d for channel %d", status, chan->chan_idx);
			break;
		}
		rtio_iodev_sqe_err(sqe, error_code);
		return;
	}

	/* Store raw data in buffer for later decoding */
	if (buffer_len < sizeof(uint16_t)) {
		LOG_ERR("Buffer too small: %d bytes needed, %d available", (int)sizeof(uint16_t),
			buffer_len);
		rtio_iodev_sqe_err(sqe, -ENOMEM);
		return;
	}

	*(uint16_t *)buffer = raw_data;
	rtio_iodev_sqe_ok(sqe, sizeof(uint16_t));
}

static const struct sensor_decoder_api pvt_tt_bh_decoder_api = {.decode = pvt_tt_bh_decode};

static int pvt_tt_bh_get_decoder(const struct device *dev, const struct sensor_decoder_api **api)
{
	if (!dev || !api) {
		return -EINVAL;
	}

	*api = &pvt_tt_bh_decoder_api;
	return 0;
}

static const struct sensor_driver_api pvt_tt_bh_driver_api = {
	.attr_set = NULL,
	.attr_get = pvt_tt_bh_attr_get,
	.trigger_set = NULL,

	/* Not implemented due to newer read (submit) and decode API preferred. */
	.sample_fetch = NULL,
	.channel_get = NULL,

	.submit = pvt_tt_bh_submit,
	.get_decoder = pvt_tt_bh_get_decoder,
};

#define DEFINE_PVT_TT_BH(id)                                                                       \
	static const struct pvt_tt_bh_config pvt_tt_bh_config_##_id = {                            \
		.num_ts = DT_PROP(DT_DRV_INST(id), num_ts),                                        \
		.num_pd = DT_PROP(DT_DRV_INST(id), num_pd),                                        \
		.num_vm = DT_PROP(DT_DRV_INST(id), num_vm),                                        \
	};                                                                                         \
                                                                                                   \
	static struct pvt_tt_bh_data pvt_tt_bh_data_##_id = {};                                    \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(id, pvt_tt_bh_init, NULL, &pvt_tt_bh_data_##_id,                     \
			      &pvt_tt_bh_config_##_id, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,   \
			      &pvt_tt_bh_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_PVT_TT_BH)
