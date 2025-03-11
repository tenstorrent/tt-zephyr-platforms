/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pcie.h"

#include <stdbool.h>

#include "reg.h"
#include "noc2axi.h"
#include "timer.h"
#include "read_only_table.h"
#include "fw_table.h"
#include "gpio.h"
#include <zephyr/sys/util.h>

#define PCIE_SERDES0_ALPHACORE_TLB 0
#define PCIE_SERDES1_ALPHACORE_TLB 1
#define PCIE_SERDES0_CTRL_TLB      2
#define PCIE_SERDES1_CTRL_TLB      3
#define PCIE_SII_REG_TLB           4
#define PCIE_TLB_CONFIG_TLB        5

#define SERDES_INST_OFFSET         0x04000000
#define PCIE_SERDES_SOC_REG_OFFSET 0x03000000
#define PCIE_TLB_CONFIG_ADDR       0x1FC00000

#define DBI_PCIE_TLB_ID                   62
#define PCIE_NOC_TLB_DATA_REG_OFFSET2(ID) PCIE_SII_A_NOC_TLB_DATA_##ID##__REG_OFFSET
#define PCIE_NOC_TLB_DATA_REG_OFFSET(ID)  PCIE_NOC_TLB_DATA_REG_OFFSET2(ID)
#define DBI_ADDR                          ((uint64_t)DBI_PCIE_TLB_ID << 58)

#define CMN_A_REG_MAP_BASE_ADDR         0xFFFFFFFFE1000000LL
#define SERDES_SS_0_A_REG_MAP_BASE_ADDR 0xFFFFFFFFE0000000LL
#define PCIE_SII_A_REG_MAP_BASE_ADDR    0xFFFFFFFFF0000000LL

#define PCIE_SII_A_NOC_TLB_DATA_62__REG_OFFSET       0x0000022C
#define PCIE_SII_A_NOC_TLB_DATA_0__REG_OFFSET        0x00000134
#define PCIE_SII_A_APP_PCIE_CTL_REG_OFFSET           0x0000005C
#define PCIE_SII_A_LTSSM_STATE_REG_OFFSET            0x00000128

typedef struct {
	uint32_t tlp_type: 5;
	uint32_t ser_np: 1;
	uint32_t ep: 1;
	uint32_t rsvd_0: 1;
	uint32_t ns: 1;
	uint32_t ro: 1;
	uint32_t tc: 3;
	uint32_t msg: 8;
	uint32_t dbi: 1;
	uint32_t atu_bypass: 1;
	uint32_t addr: 6;
} PCIE_SII_NOC_TLB_DATA_reg_t;

typedef union {
	uint32_t val;
	PCIE_SII_NOC_TLB_DATA_reg_t f;
} PCIE_SII_NOC_TLB_DATA_reg_u;

#define PCIE_SII_NOC_TLB_DATA_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t app_hold_phy_rst_axiclk: 1;
	uint32_t app_l1sub_disable_axiclk: 1;
	uint32_t app_margining_ready_axiclk: 1;
	uint32_t app_margining_software_ready_axiclk: 1;
	uint32_t app_pf_req_retry_en_axiclk: 1;
	uint32_t app_clk_req_n_axiclk: 1;
	uint32_t phy_clk_req_n_axiclk: 1;
	uint32_t rsvd_0: 23;
	uint32_t slv_rasdp_err_mode: 1;
	uint32_t mstr_rasdp_err_mode: 1;
} PCIE_SII_APP_PCIE_CTL_reg_t;

typedef union {
	uint32_t val;
	PCIE_SII_APP_PCIE_CTL_reg_t f;
} PCIE_SII_APP_PCIE_CTL_reg_u;

#define PCIE_SII_APP_PCIE_CTL_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t smlh_ltssm_state_sync: 6;
	uint32_t rdlh_link_up_sync: 1;
	uint32_t smlh_link_up_sync: 1;
} PCIE_SII_LTSSM_STATE_reg_t;

typedef union {
	uint32_t val;
	PCIE_SII_LTSSM_STATE_reg_t f;
} PCIE_SII_LTSSM_STATE_reg_u;

#define PCIE_SII_LTSSM_STATE_REG_DEFAULT (0x00000000)

PCIeInitStatus SerdesInit(uint8_t pcie_inst, PCIeDeviceType device_type,
			  uint8_t num_serdes_instance);
void ExitLoopback(void);
void EnterLoopback(void);
void CntlInit(uint8_t pcie_inst, uint8_t num_serdes_instance, uint8_t max_pcie_speed,
	      uint64_t board_id, uint32_t vendor_id);

static inline void WritePcieTlbConfigReg(const uint32_t addr, const uint32_t data)
{
	const uint8_t noc_id = 0;

	NOC2AXIWrite32(noc_id, PCIE_TLB_CONFIG_TLB, addr, data);
}

static inline void WriteDbiRegByte(const uint32_t addr, const uint8_t data)
{
	const uint8_t noc_id = 0;

	NOC2AXIWrite8(noc_id, PCIE_DBI_REG_TLB, addr, data);
}

static inline void WriteSiiReg(const uint32_t addr, const uint32_t data)
{
	const uint8_t noc_id = 0;

	NOC2AXIWrite32(noc_id, PCIE_SII_REG_TLB, addr, data);
}

static inline uint32_t ReadSiiReg(const uint32_t addr)
{
	const uint8_t noc_id = 0;

	return NOC2AXIRead32(noc_id, PCIE_SII_REG_TLB, addr);
}

static inline void WriteSerdesAlphaCoreReg(const uint8_t inst, const uint32_t addr,
					   const uint32_t data)
{
	const uint8_t noc_id = 0;
	uint8_t tlb = (inst == 0) ? PCIE_SERDES0_ALPHACORE_TLB : PCIE_SERDES1_ALPHACORE_TLB;

	NOC2AXIWrite32(noc_id, tlb, addr, data);
}

static inline uint32_t ReadSerdesAlphaCoreReg(const uint8_t inst, const uint32_t addr)
{
	const uint8_t noc_id = 0;
	uint8_t tlb = (inst == 0) ? PCIE_SERDES0_ALPHACORE_TLB : PCIE_SERDES1_ALPHACORE_TLB;

	return NOC2AXIRead32(noc_id, tlb, addr);
}

static inline void WriteSerdesCtrlReg(const uint8_t inst, const uint32_t addr, const uint32_t data)
{
	const uint8_t noc_id = 0;
	uint8_t tlb = (inst == 0) ? PCIE_SERDES0_CTRL_TLB : PCIE_SERDES1_CTRL_TLB;

	NOC2AXIWrite32(noc_id, tlb, addr, data);
}

static inline void SetupDbiAccess(void)
{
	PCIE_SII_NOC_TLB_DATA_reg_u noc_tlb_data_reg;

	noc_tlb_data_reg.val = PCIE_SII_NOC_TLB_DATA_REG_DEFAULT;
	noc_tlb_data_reg.f.dbi = 1;
	WriteSiiReg(PCIE_NOC_TLB_DATA_REG_OFFSET(DBI_PCIE_TLB_ID), noc_tlb_data_reg.val);
	/* flush out NOC_TLB_DATA register so that subsequent dbi writes are mapped to the correct
	 * location
	 */
	ReadSiiReg(PCIE_NOC_TLB_DATA_REG_OFFSET(DBI_PCIE_TLB_ID));
}

static void SetupOutboundTlbs(void)
{
	static const PCIE_SII_NOC_TLB_DATA_reg_t tlb_settings[] = {
		{
			.atu_bypass = 1,
		},
		{
			.atu_bypass = 1,
			.ro = 1,
		},
		{
			.atu_bypass = 1,
			.ns = 1,
		},
		{
			.atu_bypass = 1,
			.ro = 1,
			.ns = 1,
		},
		{},
		{
			.ro = 1,
		},
		{
			.ns = 1,
		},
		{
			.ro = 1,
			.ns = 1,
		},
	};

	for (unsigned int i = 0; i < ARRAY_SIZE(tlb_settings); i++) {
		PCIE_SII_NOC_TLB_DATA_reg_u reg = {.f = tlb_settings[i]};
		uint32_t addr = PCIE_NOC_TLB_DATA_REG_OFFSET(0) + sizeof(uint32_t) * i;

		WriteSiiReg(addr, reg.val);
	}

	ReadSiiReg(PCIE_NOC_TLB_DATA_REG_OFFSET(0)); /* Stall until writes have completed. */
}

static void ConfigurePCIeTlbs(uint8_t pcie_inst)
{
	const uint8_t ring = 0;
	const uint8_t ring0_logic_x = pcie_inst == 0 ? PCIE_INST0_LOGICAL_X : PCIE_INST1_LOGICAL_X;
	const uint8_t ring0_logic_y = PCIE_LOGICAL_Y;

	NOC2AXITlbSetup(ring, PCIE_SERDES0_ALPHACORE_TLB, ring0_logic_x, ring0_logic_y,
			CMN_A_REG_MAP_BASE_ADDR);
	NOC2AXITlbSetup(ring, PCIE_SERDES1_ALPHACORE_TLB, ring0_logic_x, ring0_logic_y,
			CMN_A_REG_MAP_BASE_ADDR + SERDES_INST_OFFSET);
	NOC2AXITlbSetup(ring, PCIE_SERDES0_CTRL_TLB, ring0_logic_x, ring0_logic_y,
			SERDES_SS_0_A_REG_MAP_BASE_ADDR + PCIE_SERDES_SOC_REG_OFFSET);
	NOC2AXITlbSetup(ring, PCIE_SERDES1_CTRL_TLB, ring0_logic_x, ring0_logic_y,
			SERDES_SS_0_A_REG_MAP_BASE_ADDR + SERDES_INST_OFFSET +
				PCIE_SERDES_SOC_REG_OFFSET);
	NOC2AXITlbSetup(ring, PCIE_SII_REG_TLB, ring0_logic_x, ring0_logic_y,
			PCIE_SII_A_REG_MAP_BASE_ADDR);
	NOC2AXITlbSetup(ring, PCIE_DBI_REG_TLB, ring0_logic_x, ring0_logic_y, DBI_ADDR);
	NOC2AXITlbSetup(ring, PCIE_TLB_CONFIG_TLB, ring0_logic_x, ring0_logic_y,
			PCIE_TLB_CONFIG_ADDR);
}

static void SetupInboundTlbs(void)
{
	EnterLoopback();
	WaitMs(1);
	/* Configure inbound 4G TLB window to point at 8,3,0x4000_0000_0000 */
	WritePcieTlbConfigReg(0x1fc00978, 0x4000);
	WritePcieTlbConfigReg(0x1fc0097c, 0x00c8);
	WritePcieTlbConfigReg(0x1fc00980, 0x0000);
	ExitLoopback();
}

static void SetupSii(void)
{
	/* For GEN4 lane margining, spec requires app_margining_ready = 1 and
	 * app_margining_software_ready = 0
	 */
	PCIE_SII_APP_PCIE_CTL_reg_u app_pcie_ctl;

	app_pcie_ctl.val = PCIE_SII_APP_PCIE_CTL_REG_DEFAULT;
	app_pcie_ctl.f.app_margining_ready_axiclk = 1;
	WriteSiiReg(PCIE_SII_A_APP_PCIE_CTL_REG_OFFSET, app_pcie_ctl.val);
}

static PCIeInitStatus PCIeInitComm(uint8_t pcie_inst, uint8_t num_serdes_instance,
				   PCIeDeviceType device_type, uint8_t max_pcie_speed)
{
	ConfigurePCIeTlbs(pcie_inst);

	PCIeInitStatus status = SerdesInit(pcie_inst, device_type, num_serdes_instance);

	if (status != PCIeInitOk) {
		return status;
	}

	SetupDbiAccess();
	CntlInit(pcie_inst, num_serdes_instance, max_pcie_speed, get_read_only_table()->board_id,
		 get_read_only_table()->vendor_id);

	SetupSii();
	SetupOutboundTlbs(); /* pcie_inst is implied by ConfigurePCIeTlbs */
	return status;
}

static void TogglePerst(void)
{
	/* GPIO34 is TRISTATE of level shifter, GPIO37 is PERST input to the level shifter */
	GpioEnableOutput(GPIO_PCIE_TRISTATE_CTRL);
	GpioEnableOutput(GPIO_CEM0_PERST);

	/* put device into reset for 1 ms */
	GpioSet(GPIO_PCIE_TRISTATE_CTRL, 1);
	GpioSet(GPIO_CEM0_PERST, 0);
	WaitMs(1);

	/* take device out of reset */
	GpioSet(GPIO_CEM0_PERST, 1);
}

static PCIeInitStatus PollForLinkUp(uint8_t pcie_inst)
{
	ARG_UNUSED(pcie_inst);

	/* timeout after 200 ms */
	uint64_t end_time = TimerTimestamp() + 500 * WAIT_1MS;
	bool training_done = false;

	do {
		PCIE_SII_LTSSM_STATE_reg_u ltssm_state;

		ltssm_state.val = ReadSiiReg(PCIE_SII_A_LTSSM_STATE_REG_OFFSET);
		training_done = ltssm_state.f.smlh_link_up_sync && ltssm_state.f.rdlh_link_up_sync;
	} while (!training_done && TimerTimestamp() < end_time);

	if (!training_done) {
		return PCIeLinkTrainTimeout;
	}

	return PCIeInitOk;
}

PCIeInitStatus PCIeInit(uint8_t pcie_inst, const FwTable_PciPropertyTable *pci_prop_table)
{
	uint8_t num_serdes_instance = pci_prop_table->num_serdes;
	PCIeDeviceType device_type =
		pci_prop_table->pcie_mode - 1; /* apply offset to match with definition in pcie.h */
	uint8_t max_pcie_speed = pci_prop_table->max_pcie_speed;

	if (device_type == RootComplex) {
		TogglePerst();
	}

	PCIeInitStatus status =
		PCIeInitComm(pcie_inst, num_serdes_instance, device_type, max_pcie_speed);
	if (status != PCIeInitOk) {
		return status;
	}

	if (device_type == RootComplex) {
		status = PollForLinkUp(pcie_inst);
		if (status != PCIeInitOk) {
			return status;
		}

		SetupInboundTlbs();

		/* re-initialize PCIe link */
		TogglePerst();
		status = PCIeInitComm(pcie_inst, num_serdes_instance, device_type, max_pcie_speed);
	}

	return status;
}
