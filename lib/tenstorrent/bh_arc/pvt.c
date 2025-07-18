/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pll.h"
#include "pvt.h"
#include "reg.h"
#include "telemetry.h"
#include "timer.h"

#include <float.h> /* for FLT_MAX */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <tenstorrent/msg_type.h>
#include <tenstorrent/msgqueue.h>
#include <tenstorrent/post_code.h>

#define SDIF_DONE_TIMEOUT_MS 10

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
} IpDataRegT;

typedef union {
	uint32_t val;
	IpDataRegT f;
} IpDataRegU;

typedef struct {
	uint32_t run_mode: 4;
	uint32_t reserved_1: 4;
	uint32_t oscillator_select: 5;
	uint32_t oscillator_enable: 3;
	uint32_t counter_divide_ratio: 2;
	uint32_t reserved_2: 2;
	uint32_t counter_gate: 2;
	uint32_t reserved_3: 10;
} PDIpCfg0T;

typedef union {
	uint32_t val;
	PDIpCfg0T f;
} PDIpCfg0U;

typedef struct {
	uint32_t run_mode: 4;
	uint32_t reserved_0: 1;
	uint32_t resolution: 2;
	uint32_t reserved_1: 25;
} TSIpCfg0T;

typedef union {
	uint8_t val;
	TSIpCfg0T f;
} TSIpCfg0U;

typedef enum {
	ValidData = 0,
	AnalogueAccess = 1,
} SampleType;

typedef struct {
	uint32_t tmr_irq_enable: 1;
	uint32_t ts_irq_enable: 1;
	uint32_t vm_irq_enable: 1;
	uint32_t pd_irq_enable: 1;
} PVT_CNTL_IRQ_EN_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_IRQ_EN_reg_t f;
} PVT_CNTL_IRQ_EN_reg_u;

#define PVT_CNTL_IRQ_EN_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t irq_en_fault: 1;
	uint32_t irq_en_done: 1;
	uint32_t rsvd_0: 1;
	uint32_t irq_en_alarm_a: 1;
	uint32_t irq_en_alarm_b: 1;
} PVT_CNTL_TS_PD_IRQ_ENABLE_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_TS_PD_IRQ_ENABLE_reg_t f;
} PVT_CNTL_TS_PD_IRQ_ENABLE_reg_u;

#define PVT_CNTL_TS_PD_IRQ_ENABLE_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t irq_en_fault: 1;
	uint32_t irq_en_done: 1;
} PVT_CNTL_VM_IRQ_ENABLE_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_VM_IRQ_ENABLE_reg_t f;
} PVT_CNTL_VM_IRQ_ENABLE_reg_u;

#define PVT_CNTL_VM_IRQ_ENABLE_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t hyst_thresh: 16;
	uint32_t alarm_thresh: 16;
} PVT_CNTL_VM_ALARMA_CFG_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_VM_ALARMA_CFG_reg_t f;
} PVT_CNTL_VM_ALARMA_CFG_reg_u;

#define PVT_CNTL_VM_ALARMA_CFG_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t hyst_thresh: 16;
	uint32_t alarm_thresh: 16;
} PVT_CNTL_VM_ALARMB_CFG_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_VM_ALARMB_CFG_reg_t f;
} PVT_CNTL_VM_ALARMB_CFG_reg_u;

#define PVT_CNTL_VM_ALARMB_CFG_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t clk_synth_lo: 8;
	uint32_t clk_synth_hi: 8;
	uint32_t clk_synth_hold: 4;
	uint32_t rsvd_0: 4;
	uint32_t clk_synth_en: 1;
} PVT_CNTL_CLK_SYNTH_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_CLK_SYNTH_reg_t f;
} PVT_CNTL_CLK_SYNTH_reg_u;

#define PVT_CNTL_CLK_SYNTH_REG_DEFAULT (0x00010000)

typedef struct {
	uint32_t sdif_busy: 1;
	uint32_t sdif_lock: 1;
} PVT_CNTL_SDIF_STATUS_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_SDIF_STATUS_reg_t f;
} PVT_CNTL_SDIF_STATUS_reg_u;

#define PVT_CNTL_SDIF_STATUS_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t sdif_wdata: 24;
	uint32_t sdif_addr: 3;
	uint32_t sdif_wrn: 1;
	uint32_t rsvd_0: 3;
	uint32_t sdif_prog: 1;
} PVT_CNTL_SDIF_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_SDIF_reg_t f;
} PVT_CNTL_SDIF_reg_u;

#define PVT_CNTL_SDIF_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t sample_data: 16;
	uint32_t sample_type: 1;
	uint32_t sample_fault: 1;
} PVT_CNTL_TS_PD_SDIF_DATA_reg_t;

typedef union {
	uint32_t val;
	PVT_CNTL_TS_PD_SDIF_DATA_reg_t f;
} PVT_CNTL_TS_PD_SDIF_DATA_reg_u;

#define PVT_CNTL_TS_PD_SDIF_DATA_REG_DEFAULT (0x00000000)

static uint32_t selected_pd_delay_chain;

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
static inline void PVTInterruptConfig(void)
{
	/* Enable Global interrupt for TS and PD */
	PVT_CNTL_IRQ_EN_reg_u irq_en;

	irq_en.val = PVT_CNTL_IRQ_EN_REG_DEFAULT;
	irq_en.f.ts_irq_enable = 1;
	irq_en.f.pd_irq_enable = 1;
	irq_en.f.vm_irq_enable = 1;
	WriteReg(PVT_CNTL_IRQ_EN_REG_ADDR, irq_en.val);

	/* Enable sources of interrupts for TS, PD, and VM */
	PVT_CNTL_TS_PD_IRQ_ENABLE_reg_u ts_irq_en;

	ts_irq_en.val = PVT_CNTL_TS_PD_IRQ_ENABLE_REG_DEFAULT;
	ts_irq_en.f.irq_en_alarm_a = 1;
	ts_irq_en.f.irq_en_alarm_b = 1;
	ts_irq_en.f.irq_en_done = 1;
	ts_irq_en.f.irq_en_fault = 1;
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, IRQ_ENABLE), ts_irq_en.val);
	}

	PVT_CNTL_VM_IRQ_ENABLE_reg_u pd_vm_irq_en;

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
	PVT_CNTL_VM_ALARMA_CFG_reg_u pvt_alarma_cfg;

	pvt_alarma_cfg.val = PVT_CNTL_VM_ALARMA_CFG_REG_DEFAULT;
	pvt_alarma_cfg.f.hyst_thresh = TempToDout(ALARM_A_THERM_TRIP_TEMP - TS_HYSTERESIS_DELTA);
	pvt_alarma_cfg.f.alarm_thresh = TempToDout(ALARM_A_THERM_TRIP_TEMP);
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, ALARMA_CFG), pvt_alarma_cfg.val);
	}

	/* Configure Alarm B */
	PVT_CNTL_VM_ALARMB_CFG_reg_u pvt_alarmb_cfg;

	pvt_alarmb_cfg.val = PVT_CNTL_VM_ALARMB_CFG_REG_DEFAULT;
	pvt_alarmb_cfg.f.hyst_thresh = TempToDout(ALARM_B_THERM_TRIP_TEMP - TS_HYSTERESIS_DELTA);
	pvt_alarmb_cfg.f.alarm_thresh = TempToDout(ALARM_B_THERM_TRIP_TEMP);
	for (uint32_t i = 0; i < NUM_TS; i++) {
		WriteReg(GET_TS_REG_ADDR(i, ALARMB_CFG), pvt_alarmb_cfg.val);
	}
}

/* PVT clocks works in range of 4-8MHz and are derived from APB clock */
/* target a PVT clock of 5 MHz */
static inline void PVTClkConfig(void)
{
	uint32_t apb_clk = GetAPBCLK();
	PVT_CNTL_CLK_SYNTH_reg_u clk_synt;

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
	PVT_CNTL_SDIF_STATUS_reg_u sdif_status;

	do {
		sdif_status.val = ReadReg(status_reg_addr);
	} while (sdif_status.f.sdif_busy == 1);
}

static void SdifWrite(uint32_t status_reg_addr, uint32_t wr_data_reg_addr, uint32_t sdif_addr,
		      uint32_t data)
{
	WaitSdifReady(status_reg_addr);
	PVT_CNTL_SDIF_reg_u sdif;

	sdif.val = PVT_CNTL_SDIF_REG_DEFAULT;
	sdif.f.sdif_addr = sdif_addr;
	sdif.f.sdif_wdata = data;
	sdif.f.sdif_wrn = 1;
	sdif.f.sdif_prog = 1;
	WriteReg(wr_data_reg_addr, sdif.val);
}

static void EnableAgingMeas(void)
{
	PDIpCfg0U ip_cfg0;

	ip_cfg0.val = 0;
	ip_cfg0.f.oscillator_enable = ALL_AGING_OSC;
	SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR, IP_CFG0_ADDR,
		  ip_cfg0.val);
}

/* setup Interrupt and clk configurations, TS, PD, VM IP configurations. */
/* Enable contiuous mode for TS and VM. For PD, run once mode should be used. */
static int PVTInit(void)
{
	SetPostCode(POST_CODE_SRC_CMFW, POST_CODE_ARC_INIT_STEP5);

	if (IS_ENABLED(CONFIG_TT_SMC_RECOVERY) || !IS_ENABLED(CONFIG_ARC)) {
		return 0;
	}

	/* Enable Process + Voltage + Thermal monitors */
	PVTInterruptConfig();
	PVTClkConfig();

	/* Configure TS */
	SdifWrite(PVT_CNTL_TS_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_TS_CMN_SDIF_REG_ADDR, IP_TMR_ADDR,
		  0x100); /* 256 cycles for TS */

	/* MODE_RUN_0, 8-bit resolution */
	TSIpCfg0U ts_ip_cfg0 = {.f.run_mode = 0, .f.resolution = 2};

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
	Wait(100 * WAIT_1US);

	return 0;
}
SYS_INIT(PVTInit, APPLICATION, 8);

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

	PVT_CNTL_TS_PD_SDIF_DATA_reg_u ts_sdif_data;

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
	/* ignore ip_done in auto_mode */
	IpDataRegU ip_data;

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

float GetAvgChipTemp(void)
{
	float ts_sum = 0;

	for (uint32_t i = 0; i < NUM_TS; i++) {
		uint16_t tmon_data;
		ReadStatus pd_read_status;

		pd_read_status = ReadTS(i, &tmon_data);
		if (pd_read_status != ReadOk) {
			/* Something went wrong! Return -FLT_MAX to indicate an error */
			return -FLT_MAX;
		}
		ts_sum += DoutToTemp(tmon_data);
	}
	return ts_sum / NUM_TS;
}

static void SelectDelayChainAndStartPDConv(uint32_t delay_chain)
{
	if (delay_chain != selected_pd_delay_chain) {
		PDIpCfg0U ip_cfg0;

		ip_cfg0.val = 0;
		ip_cfg0.f.run_mode = 0; /* MODE_PD_CNV */
		ip_cfg0.f.oscillator_enable = ALL_AGING_OSC;
		ip_cfg0.f.oscillator_select = delay_chain;
		ip_cfg0.f.counter_gate = 0x3; /* W = 255 */
		SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR,
			  IP_CFG0_ADDR, ip_cfg0.val);
		SdifWrite(PVT_CNTL_PD_CMN_SDIF_STATUS_REG_ADDR, PVT_CNTL_PD_CMN_SDIF_REG_ADDR,
			  IP_CNTL_ADDR, 0x108); /* ip_run_cont */

		/* wait until delay chain takes effect */
		WaitUs(250);
		selected_pd_delay_chain = delay_chain;
	}
}

/* return selected TS raw reading and temperature in telemetry format */
static uint8_t read_ts_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	uint32_t id = request->data[1];
	uint16_t dout = 0;

	uint8_t ret = ReadTS(id, &dout);
	response->data[1] = dout;
	response->data[2] = ConvertFloatToTelemetry(DoutToTemp(dout));
	return ret;
}

/* return selected PD raw reading and frequency in telemetry format */
static uint8_t read_pd_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	uint32_t delay_chain = request->data[1];

	SelectDelayChainAndStartPDConv(delay_chain);

	uint32_t id = request->data[2];
	uint16_t dout = 0;

	uint8_t ret = ReadPD(id, &dout);

	response->data[1] = dout;
	response->data[2] = ConvertFloatToTelemetry(DoutToFreq(dout));
	return ret;
}

/* return selected VM raw reading and voltage in mV */
static uint8_t read_vm_handler(uint32_t msg_code, const struct request *request,
			       struct response *response)
{
	uint32_t id = request->data[1];
	uint16_t dout = 0;

	uint8_t ret = ReadVM(id, &dout);
	response->data[1] = dout;
	response->data[2] = DoutToVolt(dout) * 1000;
	return ret;
}

REGISTER_MESSAGE(MSG_TYPE_READ_TS, read_ts_handler);
REGISTER_MESSAGE(MSG_TYPE_READ_PD, read_pd_handler);
REGISTER_MESSAGE(MSG_TYPE_READ_VM, read_vm_handler);
