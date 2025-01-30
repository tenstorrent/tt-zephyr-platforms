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

#define BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_REG_ADDR  \
	0x00300104
#define BH_PCIE_ATU_CAP_IATU_UPPER_TARGET_ADDR_OFF_INBOUND_1_REG_ADDR \
	0x00300318
#define BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_1_REG_ADDR  \
	0x00300304
#define BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR    0x00000710
#define BH_PCIE_PORT_LOGIC_GEN2_CTRL_OFF_REG_ADDR         0x0000080C
#define BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR   0x0000007C
#define BH_PCIE_TYPE0_HDR_DBI2_BAR0_MASK_REG_REG_ADDR     0x00100010
#define BH_PCIE_TYPE0_HDR_DBI2_BAR5_MASK_REG_REG_ADDR     0x00100024
#define BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR    0x000008BC
#define BH_PCIE_TYPE0_HDR_CLASS_CODE_REVISION_ID_REG_ADDR 0x00000008
#define BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_REG_ADDR        0x00000040
#define BH_PCIE_TYPE0_HDR_SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG_REG_ADDR \
	0x0000002C
#define BH_PCIE_PCIE_CAP_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_REG_ADDR \
	0x00000070
#define BH_PCIE_PL32G_CAP_PL32G_EXT_CAP_HDR_REG_REG_ADDR 0x000001F0
#define BH_PCIE_PORT_LOGIC_ACK_F_ASPM_CTRL_OFF_REG_ADDR  0x0000070C
#define BH_PCIE_PTM_CAP_PTM_CAP_OFF_REG_ADDR             0x00000378
#define BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR     0x00000890
#define BH_PCIE_PORT_LOGIC_GEN3_EQ_CONTROL_OFF_REG_ADDR  0x000008A8
#define BH_PCIE_PORT_LOGIC_PIPE_LOOPBACK_CONTROL_OFF_REG_ADDR      \
	0x000008B8
#define BH_PCIE_PORT_LOGIC_SYMBOL_TIMER_FILTER_1_OFF_REG_ADDR      \
	0x0000071C
#define BH_PCIE_PORT_LOGIC_FILTER_MASK_2_OFF_REG_ADDR    0x00000720
#define BH_PCIE_PCIE_CAP_LINK_CAPABILITIES2_REG_REG_ADDR 0x0000009C
#define BH_PCIE_PCIE_CAP_LINK_CONTROL2_LINK_STATUS2_REG_REG_ADDR   \
	0x000000A0

#define PCIE_SERDES0_ALPHACORE_TLB 0
#define PCIE_SERDES1_ALPHACORE_TLB 1
#define PCIE_SERDES0_CTRL_TLB      2
#define PCIE_SERDES1_CTRL_TLB      3
#define PCIE_SII_REG_TLB           4
#define PCIE_TLB_CONFIG_TLB        5

#define AXI2NOC_TLB_ADDR_WIDTH 24

#define SERDES_INST_OFFSET         0x04000000
#define PCIE_SERDES_SRAM_OFFSET    0x01400000
#define PCIE_SERDES_SOC_REG_OFFSET 0x03000000
#define PCIE_TLB_CONFIG_ADDR       0x1FC00000

#define DBI_PCIE_TLB_ID                   62
#define PCIE_NOC_TLB_DATA_REG_OFFSET2(ID) PCIE_SII_A_NOC_TLB_DATA_##ID##__REG_OFFSET
#define PCIE_NOC_TLB_DATA_REG_OFFSET(ID)  PCIE_NOC_TLB_DATA_REG_OFFSET2(ID)
#define DBI_ADDR                          ((uint64_t)DBI_PCIE_TLB_ID << 58)

#define CMN_A_REG_MAP_BASE_ADDR         0xFFFFFFFFE1000000LL
#define SERDES_SS_0_A_REG_MAP_BASE_ADDR 0xFFFFFFFFE0000000LL
#define PCIE_SII_A_REG_MAP_BASE_ADDR    0xFFFFFFFFF0000000LL

#define RESET_UNIT_PCIE_RESET_REG_ADDR               0x80030004
#define PCIE_SII_A_NOC_TLB_DATA_62__REG_OFFSET       0x0000022C
#define PCIE_SII_A_NOC_TLB_DATA_0__REG_OFFSET        0x00000134
#define PCIE_SII_A_APP_PCIE_CTL_REG_OFFSET           0x0000005C
#define PCIE_SII_A_LTSSM_STATE_REG_OFFSET            0x00000128

typedef struct {
	uint32_t msg_code: 8;
	uint32_t bar_num: 3;
	uint32_t reserved_11_12: 2;
	uint32_t msg_type_match_mode: 1;
	uint32_t tc_match_en: 1;
	uint32_t td_match_en: 1;
	uint32_t attr_match_en: 1;
	uint32_t reserved_17_18: 2;
	uint32_t func_num_match_en: 1;
	uint32_t reserved_20_20: 1;
	uint32_t msg_code_match_en: 1;
	uint32_t reserved_22_22: 1;
	uint32_t single_addr_loc_trans_en: 1;
	uint32_t response_code: 2;
	uint32_t reserved_26_26: 1;
	uint32_t fuzzy_type_match_code: 1;
	uint32_t cfg_shift_mode: 1;
	uint32_t invert_mode: 1;
	uint32_t match_mode: 1;
	uint32_t region_en: 1;
} BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_reg_t f;
} BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_reg_u;

#define BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_REG_DEFAULT (0x00000000)

typedef struct {
	uint32_t vendor_specific_dllp_req: 1;
	uint32_t scramble_disable: 1;
	uint32_t loopback_enable: 1;
	uint32_t reset_assert: 1;
	uint32_t rsvdp_4: 1;
	uint32_t dll_link_en: 1;
	uint32_t link_disable: 1;
	uint32_t fast_link_mode: 1;
	uint32_t link_rate: 4;
	uint32_t rsvdp_12: 4;
	uint32_t link_capable: 6;
	uint32_t reserved_22_23: 2;
	uint32_t beacon_enable: 1;
	uint32_t corrupt_lcrc_enable: 1;
	uint32_t extended_synch: 1;
	uint32_t transmit_lane_reversale_enable: 1;
	uint32_t rsvdp_28: 4;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_REG_DEFAULT       \
	(0x001F0120)

typedef struct {
	uint32_t fast_training_seq: 8;
	uint32_t num_of_lanes: 5;
	uint32_t pre_det_lane: 3;
	uint32_t auto_lane_flip_ctrl_en: 1;
	uint32_t direct_speed_change: 1;
	uint32_t config_phy_tx_change: 1;
	uint32_t config_tx_comp_rx: 1;
	uint32_t sel_deemphasis: 1;
	uint32_t gen1_ei_inference: 1;
	uint32_t select_deemph_var_mux: 1;
	uint32_t selectable_deemph_bit_mux: 1;
	uint32_t lane_under_test: 4;
	uint32_t eq_for_loopback: 1;
	uint32_t tx_mod_cmpl_pattern_for_loopback: 1;
	uint32_t force_lane_flip: 1;
	uint32_t support_mod_ts: 1;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_REG_DEFAULT (0x300110FF)

typedef struct {
	uint32_t pcie_cap_max_link_speed: 4;
	uint32_t pcie_cap_max_link_width: 6;
	uint32_t pcie_cap_active_state_link_pm_support: 2;
	uint32_t pcie_cap_l0s_exit_latency: 3;
	uint32_t pcie_cap_l1_exit_latency: 3;
	uint32_t pcie_cap_clock_power_man: 1;
	uint32_t pcie_cap_surprise_down_err_rep_cap: 1;
	uint32_t pcie_cap_dll_active_rep_cap: 1;
	uint32_t pcie_cap_link_bw_not_cap: 1;
	uint32_t pcie_cap_aspm_opt_compliance: 1;
	uint32_t rsvdp_23: 1;
	uint32_t pcie_cap_port_num: 8;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_t f;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_u;

#define BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_REG_DEFAULT      \
	(0x00477D05)

typedef struct {
	uint32_t dbi_ro_wr_en: 1;
	uint32_t default_target: 1;
	uint32_t ur_ca_mask_4_trgt1: 1;
	uint32_t simplified_replay_timer: 1;
	uint32_t reserved_4_4: 1;
	uint32_t ari_device_number: 1;
	uint32_t cplq_mng_en: 1;
	uint32_t cfg_tlp_bypass_en_reg: 1;
	uint32_t config_limit_reg: 10;
	uint32_t target_above_config_limit_reg: 2;
	uint32_t p2p_track_cpl_to_reg: 1;
	uint32_t p2p_err_rpt_ctrl: 1;
	uint32_t port_logic_wr_disable: 1;
	uint32_t ras_reg_pf0_only: 1;
	uint32_t rasdes_reg_pf0_only: 1;
	uint32_t err_inj_wr_disable: 1;
	uint32_t rsvdp_26: 6;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_REG_DEFAULT       \
	(0x000BFF58)

typedef struct {
	uint32_t revision_id: 8;
	uint32_t program_interface: 8;
	uint32_t subclass_code: 8;
	uint32_t base_class_code: 8;
} BH_PCIE_TYPE0_HDR_HDL_PATH_F20946C1_CLASS_CODE_REVISION_ID_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_TYPE0_HDR_HDL_PATH_F20946C1_CLASS_CODE_REVISION_ID_reg_t f;
} BH_PCIE_TYPE0_HDR_HDL_PATH_F20946C1_CLASS_CODE_REVISION_ID_reg_u;

#define BH_PCIE_TYPE0_HDR_HDL_PATH_F20946C1_CLASS_CODE_REVISION_ID_REG_DEFAULT    \
	(0x00000000)

typedef struct {
	uint32_t pm_cap_id: 8;
	uint32_t pm_next_pointer: 8;
	uint32_t pm_spec_ver: 3;
	uint32_t pme_clk: 1;
	uint32_t reserved_20_20: 1;
	uint32_t dsi: 1;
	uint32_t aux_curr: 3;
	uint32_t d1_support: 1;
	uint32_t d2_support: 1;
	uint32_t pme_support: 5;
} BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_reg_t f;
} BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_reg_u;

#define BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_REG_DEFAULT (0xDFC35001)

typedef struct {
	uint32_t pcie_cap_id: 8;
	uint32_t pcie_cap_next_ptr: 8;
	uint32_t pcie_cap_reg: 4;
	uint32_t pcie_dev_port_type: 4;
	uint32_t pcie_slot_imp: 1;
	uint32_t pcie_int_msg_num: 5;
	uint32_t rsvd: 1;
	uint32_t rsvdp_31: 1;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_reg_t;

typedef union {
	uint32_t val;

	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_reg_t
		f;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_reg_u;

#define BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_REG_DEFAULT \
	(0x0002B010)

typedef struct {
	uint32_t extended_cap_id: 16;
	uint32_t cap_version: 4;
	uint32_t next_offset: 12;
} BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_reg_t f;
} BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_reg_u;

#define BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_REG_DEFAULT      \
	(0x2301001E)

typedef struct {
	uint32_t extended_cap_id: 16;
	uint32_t cap_version: 4;
	uint32_t next_offset: 12;
} BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_reg_t f;
} BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_reg_u;

#define BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_REG_DEFAULT     \
	(0x2201002A)

typedef struct {
	uint32_t ack_freq: 8;
	uint32_t ack_n_fts: 8;
	uint32_t common_clk_n_fts: 8;
	uint32_t l0s_entrance_latency: 3;
	uint32_t l1_entrance_latency: 3;
	uint32_t enter_aspm: 1;
	uint32_t aspm_l1_timer_enable: 1;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_ACK_F_ASPM_CTRL_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_ACK_F_ASPM_CTRL_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_ACK_F_ASPM_CTRL_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_ACK_F_ASPM_CTRL_OFF_REG_DEFAULT      \
	(0x9BFFFF00)

typedef struct {
	uint32_t ptm_req_capable: 1;
	uint32_t ptm_res_capable: 1;
	uint32_t ptm_root_capable: 1;
	uint32_t eptm_capable: 1;
	uint32_t rsvdp_4: 4;
	uint32_t ptm_clk_gran: 8;
	uint32_t rsvdp_16: 16;
} BH_PCIE_PTM_CAP_HDL_PATH_FC87603D_PTM_CAP_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PTM_CAP_HDL_PATH_FC87603D_PTM_CAP_OFF_reg_t f;
} BH_PCIE_PTM_CAP_HDL_PATH_FC87603D_PTM_CAP_OFF_reg_u;

#define BH_PCIE_PTM_CAP_HDL_PATH_FC87603D_PTM_CAP_OFF_REG_DEFAULT (0x0000100F)

typedef struct {
	uint32_t gen3_zrxdc_noncompl: 1;
	uint32_t no_seed_value_change: 1;
	uint32_t rsvdp_2: 6;
	uint32_t disable_scrambler_gen_3: 1;
	uint32_t eq_phase_2_3: 1;
	uint32_t eq_eieos_cnt: 1;
	uint32_t eq_redo: 1;
	uint32_t rxeq_ph01_en: 1;
	uint32_t rxeq_rgrdless_rxts: 1;
	uint32_t rsvdp_14: 2;
	uint32_t gen3_equalization_disable: 1;
	uint32_t gen3_dllp_xmt_delay_disable: 1;
	uint32_t gen3_dc_balance_disable: 1;
	uint32_t rsvdp_19: 2;
	uint32_t auto_eq_disable: 1;
	uint32_t usp_send_8gt_eq_ts2_disable: 1;
	uint32_t gen3_eq_invreq_eval_diff_disable: 1;
	uint32_t rate_shadow_sel: 2;
	uint32_t rsvdp_26: 6;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_REG_DEFAULT         \
	(0x00402001)

typedef struct {
	uint32_t gen3_eq_fb_mode: 4;
	uint32_t gen3_eq_phase23_exit_mode: 1;
	uint32_t gen3_eq_eval_2ms_disable: 1;
	uint32_t gen3_lower_rate_eq_redo_enable: 1;
	uint32_t rsvdp_7: 1;
	uint32_t gen3_eq_pset_req_vec: 16;
	uint32_t gen3_eq_fom_inc_initial_eval: 1;
	uint32_t gen3_eq_pset_req_as_coef: 1;
	uint32_t gen3_req_send_consec_eieos_for_pset_map: 1;
	uint32_t gen3_eq_req_num: 3;
	uint32_t gen3_support_finite_eq_request: 1;
	uint32_t rsvdp_31: 1;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_REG_DEFAULT      \
	(0x0D001060)

typedef struct {
	uint32_t lpbk_rxvalid: 16;
	uint32_t rxstatus_lane: 6;
	uint32_t rsvdp_22: 2;
	uint32_t rxstatus_value: 3;
	uint32_t rsvdp_27: 4;
	uint32_t pipe_loopback: 1;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_REG_DEFAULT \
	(0x0000FFFF)

typedef struct {
	uint32_t skp_int_val: 11;
	uint32_t eidle_timer: 4;
	uint32_t disable_fc_wd_timer: 1;
	uint32_t mask_radm_1: 16;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_reg_t f;
} BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_reg_u;

#define BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_REG_DEFAULT \
	(0x00000140)

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

typedef struct {
	uint32_t sd_mode_sel_0: 1;
	uint32_t sd_mode_sel_1: 1;
	uint32_t reserved_2: 1;
	uint32_t mux_sel: 2;
	uint32_t master_sel_0: 2;
	uint32_t master_sel_1: 2;
	uint32_t master_sel_2: 2;
	uint32_t reserved_31_11: 21;
} RESET_UNIT_PCIE_MISC_CNTL3_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_PCIE_MISC_CNTL3_reg_t f;
} RESET_UNIT_PCIE_MISC_CNTL3_reg_u;

typedef struct {
	uint32_t device_type: 1;
	uint32_t rx_lane_flip_en: 1;
	uint32_t tx_lane_flip_en: 1;
	uint32_t reserved_3: 1;
	uint32_t app_sris_mode: 1;
	uint32_t reserved_7_5: 3;
	uint32_t outband_pwrup_cmd: 1;
	uint32_t apps_pm_xmt_pme: 1;
	uint32_t sys_aux_pwr_det: 1;
	uint32_t app_req_entr_l1: 1;
	uint32_t app_ready_entr_l23: 1;
	uint32_t app_req_exit_l1: 1;
	uint32_t app_xfer_pending: 1;
	uint32_t app_init_rst: 1;
	uint32_t app_req_retry_en: 1;
	uint32_t app_clk_pm_en: 1;
	uint32_t power_up_rst_n: 1;
	uint32_t sys_int: 1;
	uint32_t reserved_31_20: 12;
} RESET_UNIT_PCIE_MISC_CNTL2_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_PCIE_MISC_CNTL2_reg_t f;
} RESET_UNIT_PCIE_MISC_CNTL2_reg_u;

typedef struct {
	uint32_t app_ltssm_enable: 4;
} RESET_UNIT_PCIE_RESET_reg_t;

typedef union {
	uint32_t val;
	RESET_UNIT_PCIE_RESET_reg_t f;
} RESET_UNIT_PCIE_RESET_reg_u;

typedef struct {
	uint32_t lsref_div_sel_nt: 1;
	uint32_t vcodiv_sel_nt: 1;
	uint32_t pclk_sel_nt: 1;
	uint32_t ppm_meas_div_nt: 2;
	uint32_t vco_adapt_div_nt: 2;
	uint32_t master_fast_div_nt: 2;
	uint32_t master_fast_sel_nt: 1;
	uint32_t master_apb_sel_nt: 1;
	uint32_t sram_fast_sel_nt: 1;
	uint32_t refclk_sel_nt: 1;
	uint32_t refdet_sel_nt: 1;
} CMN_SWITCHCLK_DBE_CMN_reg_t;

typedef union {
	uint32_t val;
	CMN_SWITCHCLK_DBE_CMN_reg_t f;
} CMN_SWITCHCLK_DBE_CMN_reg_u;

typedef struct {
	uint32_t pcie_lane_off: 8;
	uint32_t phy_status_source: 2;
} SERDES_CTRL_PCIE_LANE_OFF_reg_t;

typedef union {
	uint32_t val;
	SERDES_CTRL_PCIE_LANE_OFF_reg_t f;
} SERDES_CTRL_PCIE_LANE_OFF_reg_u;

typedef struct {
	uint32_t rsvdp_0: 1;
	uint32_t pcie_cap_support_link_speed_vector: 7;
	uint32_t pcie_cap_cross_link_support: 1;
	uint32_t rsvdp_9: 14;
	uint32_t pcie_cap_retimer_pre_det_support: 1;
	uint32_t pcie_cap_two_retimers_pre_det_support: 1;
	uint32_t rsvdp_25: 6;
	uint32_t reserved_31_31: 1;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES2_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES2_REG_reg_t f;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES2_REG_reg_u;

typedef struct {
	uint32_t pcie_cap_target_link_speed: 4;
	uint32_t pcie_cap_enter_compliance: 1;
	uint32_t pcie_cap_hw_auto_speed_disable: 1;
	uint32_t pcie_cap_sel_deemphasis: 1;
	uint32_t pcie_cap_tx_margin: 3;
	uint32_t pcie_cap_enter_modified_compliance: 1;
	uint32_t pcie_cap_compliance_sos: 1;
	uint32_t pcie_cap_compliance_preset: 4;
	uint32_t pcie_cap_curr_deemphasis: 1;
	uint32_t pcie_cap_eq_cpl: 1;
	uint32_t pcie_cap_eq_cpl_p1: 1;
	uint32_t pcie_cap_eq_cpl_p2: 1;
	uint32_t pcie_cap_eq_cpl_p3: 1;
	uint32_t pcie_cap_link_eq_req: 1;
	uint32_t pcie_cap_retimer_pre_det: 1;
	uint32_t pcie_cap_two_retimers_pre_det: 1;
	uint32_t pcie_cap_crosslink_resolution: 2;
	uint32_t rsvdp_26: 2;
	uint32_t reserved_28_31: 4;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CONTROL2_LINK_STATUS2_REG_reg_t;

typedef union {
	uint32_t val;
	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CONTROL2_LINK_STATUS2_REG_reg_t f;
} BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CONTROL2_LINK_STATUS2_REG_reg_u;

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

static void ForceLinkSpeed(uint8_t max_pcie_speed)
{
	if (max_pcie_speed >= 1 && max_pcie_speed <= 5) {
		BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES2_REG_reg_u
			link_cap2;
		link_cap2.val = ReadDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CAPABILITIES2_REG_REG_ADDR);
		link_cap2.f.pcie_cap_support_link_speed_vector = (1 << max_pcie_speed) - 1;
		WriteDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CAPABILITIES2_REG_REG_ADDR,
			link_cap2.val);

		BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_u
			link_cap;
		link_cap.val = ReadDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR);
		link_cap.f.pcie_cap_max_link_speed = max_pcie_speed;
		WriteDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR,
			link_cap.val);

		BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CONTROL2_LINK_STATUS2_REG_reg_u
			link_control2_link_status2;
		link_control2_link_status2.val = ReadDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CONTROL2_LINK_STATUS2_REG_REG_ADDR);
		link_control2_link_status2.f.pcie_cap_target_link_speed = max_pcie_speed;
		WriteDbiReg(
			BH_PCIE_PCIE_CAP_LINK_CONTROL2_LINK_STATUS2_REG_REG_ADDR,
			link_control2_link_status2.val);
	}
}

static void ProgramIatu(void)
{
	/* BAR0 iATU programming, map inbound BAR0 access to 0x0, no need to program target addr
	 * since default is 0.
	 */
	BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_reg_u iatu_region_ctrl_2;

	iatu_region_ctrl_2.val = 0;
	iatu_region_ctrl_2.f.region_en = 1;
	iatu_region_ctrl_2.f.match_mode = 1; /* BAR match mode */
	iatu_region_ctrl_2.f.fuzzy_type_match_code = 1;
	iatu_region_ctrl_2.f.bar_num = 0;
	WriteDbiReg(
		BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_0_REG_ADDR,
		iatu_region_ctrl_2.val);

	/* BAR4 iATU programming, map inbound BAR4 access to 0x20_00000000 */
	uint32_t upper_target_addr = 0x20;

	WriteDbiReg(
		BH_PCIE_ATU_CAP_IATU_UPPER_TARGET_ADDR_OFF_INBOUND_1_REG_ADDR,
		upper_target_addr);
	iatu_region_ctrl_2.f.bar_num = 4;
	WriteDbiReg(
		BH_PCIE_ATU_CAP_IATU_REGION_CTRL_2_OFF_INBOUND_1_REG_ADDR,
		iatu_region_ctrl_2.val);
}

/* reduce PCIe width to 8 lanes */
static void ReducePCIeWidth(void)
{
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_u
		port_link_ctrl;
	port_link_ctrl.val =
		BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_REG_DEFAULT;
	port_link_ctrl.f.link_capable = 0xf;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR,
		    port_link_ctrl.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_reg_u gen2_ctrl_off;

	gen2_ctrl_off.val =
		BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN2_CTRL_OFF_REG_DEFAULT;
	gen2_ctrl_off.f.num_of_lanes = 0x8;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_GEN2_CTRL_OFF_REG_ADDR,
		    gen2_ctrl_off.val);

	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_u link_cap;

	link_cap.val =
		BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_REG_DEFAULT;
	link_cap.f.pcie_cap_max_link_width = 0x8;
	WriteDbiReg(BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR,
		    link_cap.val);
}

static void CntlInit(uint8_t pcie_inst, uint8_t num_serdes_instance, uint8_t max_pcie_speed)
{
	ProgramIatu();

	ForceLinkSpeed(max_pcie_speed);

	/* configure BAR0 size to 512MB */
	WriteDbiReg(BH_PCIE_TYPE0_HDR_DBI2_BAR0_MASK_REG_REG_ADDR,
		    512 * 1024 * 1024 - 1);

	/* configure BAR4 size to 32GB */
	/* BAR5_MASK_REG has the top bits of a 64-bit BAR4 mask */
	WriteDbiReg(BH_PCIE_TYPE0_HDR_DBI2_BAR5_MASK_REG_REG_ADDR,
		    0x7);

	/* Enable write to RO registers using DBI */
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_u
		misc_control_1_off;
	misc_control_1_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR);
	misc_control_1_off.f.dbi_ro_wr_en = 1;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR,
		    misc_control_1_off.val);

	BH_PCIE_TYPE0_HDR_HDL_PATH_F20946C1_CLASS_CODE_REVISION_ID_reg_u
		class_code_rev_id = {0};
	class_code_rev_id.f.base_class_code = 0x12; /* Processing Accelerator */
	class_code_rev_id.f.revision_id = 0x0;
	WriteDbiReg(
		BH_PCIE_TYPE0_HDR_CLASS_CODE_REVISION_ID_REG_ADDR,
		class_code_rev_id.val);

	/* set Vaux auxiliary current requirements to 0 */
	BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_reg_u nxt_ptr_reg;

	nxt_ptr_reg.val = ReadDbiReg(
		BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_REG_ADDR);
	nxt_ptr_reg.f.aux_curr = 0;
	WriteDbiReg(BH_PCIE_PM_CAP_CAP_ID_NXT_PTR_REG_REG_ADDR,
		    nxt_ptr_reg.val);

	/* Extract the board_type from the board_id */
	WriteDbiReg(
		BH_PCIE_TYPE0_HDR_SUBSYSTEM_ID_SUBSYSTEM_VENDOR_ID_REG_REG_ADDR,
		((get_read_only_table()->board_id >> 36) & 0xFFFF) << 16 |
			get_read_only_table()->vendor_id);

	/* Hide MSI-X capability */
	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_reg_u
		pcie_cap_reg;
	pcie_cap_reg.val = ReadDbiReg(
		BH_PCIE_PCIE_CAP_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_REG_ADDR);
	pcie_cap_reg.f.pcie_cap_next_ptr = 0;
	WriteDbiReg(
		BH_PCIE_PCIE_CAP_PCIE_CAP_ID_PCIE_NEXT_CAP_PTR_PCIE_CAP_REG_REG_ADDR,
		pcie_cap_reg.val);

	/* Hide L1ss capability since CLKREQ is not connected */
	BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_reg_u
		l1sub_cap_hdr;
	l1sub_cap_hdr.val =
		BH_PCIE_L1SUB_CAP_HDL_PATH_DDA86B41_L1SUB_CAP_HEADER_REG_REG_DEFAULT;
	BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_reg_u
		pl32g_ext_cap_hdr;
	pl32g_ext_cap_hdr.val =
		BH_PCIE_PL32G_CAP_HDL_PATH_84B99C76_PL32G_EXT_CAP_HDR_REG_REG_DEFAULT;
	pl32g_ext_cap_hdr.f.next_offset = l1sub_cap_hdr.f.next_offset;
	WriteDbiReg(
		BH_PCIE_PL32G_CAP_PL32G_EXT_CAP_HDR_REG_REG_ADDR,
		pl32g_ext_cap_hdr.val);

	/* Set L1 entrance latency to max */
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_ACK_F_ASPM_CTRL_OFF_reg_u
		aspm_ctrl_off;
	aspm_ctrl_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_ACK_F_ASPM_CTRL_OFF_REG_ADDR);
	aspm_ctrl_off.f.l1_entrance_latency = 7;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_ACK_F_ASPM_CTRL_OFF_REG_ADDR,
		    aspm_ctrl_off.val);

	/* Disable ASPMs: https://tenstorrent.atlassian.net/browse/SYS-663,
	 * https://tenstorrent.atlassian.net/browse/SYS-717
	 */
	BH_PCIE_PCIE_CAP_HDL_PATH_F20946C1_LINK_CAPABILITIES_REG_reg_u link_cap;

	link_cap.val = ReadDbiReg(
		BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR);
	link_cap.f.pcie_cap_active_state_link_pm_support = 0x0;
	WriteDbiReg(BH_PCIE_PCIE_CAP_LINK_CAPABILITIES_REG_REG_ADDR,
		    link_cap.val);

	/* configure PTM for EP */
	BH_PCIE_PTM_CAP_HDL_PATH_FC87603D_PTM_CAP_OFF_reg_u ptm_cap_off;

	ptm_cap_off.val =
		ReadDbiReg(BH_PCIE_PTM_CAP_PTM_CAP_OFF_REG_ADDR);
	ptm_cap_off.f.ptm_root_capable = 0;
	ptm_cap_off.f.ptm_res_capable = 0;
	ptm_cap_off.f.ptm_clk_gran = 0;
	WriteDbiReg(BH_PCIE_PTM_CAP_PTM_CAP_OFF_REG_ADDR,
		    ptm_cap_off.val);

	/* Override preset request vector to include all presets for GEN3, GEN4, GEN5 */
	for (uint8_t pcie_gen = 3; pcie_gen <= 5; pcie_gen++) {
		uint8_t rate_shadow_sel = pcie_gen - 3;

		WriteDbiRegByte(
			BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR +
				3,
			rate_shadow_sel);

		if (pcie_gen > 3) {
			BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_u
				gen3_related_off;
			gen3_related_off.val =
				BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_REG_DEFAULT;
			gen3_related_off.f.rate_shadow_sel = rate_shadow_sel;
			gen3_related_off.f.usp_send_8gt_eq_ts2_disable =
				1; /* this fixes the gen4-gen5 eq failure in compliance test
				    * TD_2_11: https://tenstorrent.atlassian.net/browse/SYS-758
				    */
			WriteDbiReg(
				BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR,
				gen3_related_off.val);
		}

		BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_reg_u
			gen3_eq_control_off;
		gen3_eq_control_off.val =
			BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_EQ_CONTROL_OFF_REG_DEFAULT;
		gen3_eq_control_off.f.gen3_eq_pset_req_vec = 0x3E0;
		gen3_eq_control_off.f.gen3_eq_fb_mode =
			0x1; /* disable directional mode:
			      * https://tenstorrent.atlassian.net/browse/SYS-524
			      */
		gen3_eq_control_off.f.gen3_eq_phase23_exit_mode =
			0x1; /* exit to EQ phase3 after EQ phase2 timeout */
		WriteDbiReg(
			BH_PCIE_PORT_LOGIC_GEN3_EQ_CONTROL_OFF_REG_ADDR,
			gen3_eq_control_off.val);
	}

	if (num_serdes_instance == 1) {
		ReducePCIeWidth();
	}

	/* Assert LTSSM enable to kick off PCIe link training, do not wait for completion */
	RESET_UNIT_PCIE_RESET_reg_u pcie_reset_reg;

	pcie_reset_reg.val = ReadReg(RESET_UNIT_PCIE_RESET_REG_ADDR);
	pcie_reset_reg.f.app_ltssm_enable |= 1 << pcie_inst;
	WriteReg(RESET_UNIT_PCIE_RESET_REG_ADDR, pcie_reset_reg.val);
}

void EnterLoopback(void)
{
	/* PCIe DM controller databook: C.2 Local Digital Loopback (PIPE) */
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_u
		gen3_related_off;
	gen3_related_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR);
	gen3_related_off.f.gen3_equalization_disable = 1;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR,
		    gen3_related_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_reg_u
		pipe_loopback_control_off;
	pipe_loopback_control_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_PIPE_LOOPBACK_CONTROL_OFF_REG_ADDR);
	pipe_loopback_control_off.f.pipe_loopback = 1;
	WriteDbiReg(
		BH_PCIE_PORT_LOGIC_PIPE_LOOPBACK_CONTROL_OFF_REG_ADDR,
		pipe_loopback_control_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_u
		port_link_ctrl;
	port_link_ctrl.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR);
	port_link_ctrl.f.loopback_enable = 1;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR,
		    port_link_ctrl.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_u
		misc_control_1_off;
	misc_control_1_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR);
	misc_control_1_off.f.default_target = 1;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR,
		    misc_control_1_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_reg_u
		symbol_timer_filter_1_off;
	symbol_timer_filter_1_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_SYMBOL_TIMER_FILTER_1_OFF_REG_ADDR);
	symbol_timer_filter_1_off.f.mask_radm_1 = 0xffff;
	WriteDbiReg(
		BH_PCIE_PORT_LOGIC_SYMBOL_TIMER_FILTER_1_OFF_REG_ADDR,
		symbol_timer_filter_1_off.val);

	WriteDbiReg(BH_PCIE_PORT_LOGIC_FILTER_MASK_2_OFF_REG_ADDR,
		    0xffffffff);
}

void ExitLoopback(void)
{
	/* PCIe DM controller databook: C.2 Local Digital Loopback (PIPE) */
	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_GEN3_RELATED_OFF_reg_u
		gen3_related_off;
	gen3_related_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR);
	gen3_related_off.f.gen3_equalization_disable = 0;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_GEN3_RELATED_OFF_REG_ADDR,
		    gen3_related_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PIPE_LOOPBACK_CONTROL_OFF_reg_u
		pipe_loopback_control_off;
	pipe_loopback_control_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_PIPE_LOOPBACK_CONTROL_OFF_REG_ADDR);
	pipe_loopback_control_off.f.pipe_loopback = 0;
	WriteDbiReg(
		BH_PCIE_PORT_LOGIC_PIPE_LOOPBACK_CONTROL_OFF_REG_ADDR,
		pipe_loopback_control_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_PORT_LINK_CTRL_OFF_reg_u
		port_link_ctrl;
	port_link_ctrl.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR);
	port_link_ctrl.f.loopback_enable = 0;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_PORT_LINK_CTRL_OFF_REG_ADDR,
		    port_link_ctrl.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_MISC_CONTROL_1_OFF_reg_u
		misc_control_1_off;
	misc_control_1_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR);
	misc_control_1_off.f.default_target = 0;
	WriteDbiReg(BH_PCIE_PORT_LOGIC_MISC_CONTROL_1_OFF_REG_ADDR,
		    misc_control_1_off.val);

	BH_PCIE_PORT_LOGIC_HDL_PATH_F9FD0F72_SYMBOL_TIMER_FILTER_1_OFF_reg_u
		symbol_timer_filter_1_off;
	symbol_timer_filter_1_off.val = ReadDbiReg(
		BH_PCIE_PORT_LOGIC_SYMBOL_TIMER_FILTER_1_OFF_REG_ADDR);
	symbol_timer_filter_1_off.f.mask_radm_1 = 0x0;
	WriteDbiReg(
		BH_PCIE_PORT_LOGIC_SYMBOL_TIMER_FILTER_1_OFF_REG_ADDR,
		symbol_timer_filter_1_off.val);

	WriteDbiReg(BH_PCIE_PORT_LOGIC_FILTER_MASK_2_OFF_REG_ADDR,
		    0x0);
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
	CntlInit(pcie_inst, num_serdes_instance, max_pcie_speed);

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
