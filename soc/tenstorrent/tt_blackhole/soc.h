/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SOC_TENSTORRENT_TT_BLACKHOLE_SOC_H_
#define SOC_TENSTORRENT_TT_BLACKHOLE_SOC_H_

/*
 * Note: this file is needed because zephyr/drivers/interrupt_controller/intc_dw.c unconditionally
 * includes soc.h.
 */

#if defined(CONFIG_SOC_TT_BLACKHOLE_SMC)

/*
 * DT fixups
 * Should fix the upstream snps,designware-intc driver to drop the 'sense' field
 * of the interrupts property based on whether the parent interrupt controller has
 * 2 interrupt cells (rather than 3). The reason being that snps,arcv2-intc has
 * a constant #interrupt-cells = <2>, while the interrupt controllers we've hung
 * on it require #interrupt-cells = <3>.
 */
#define DT_N_S_ictl_800c0000_IRQ_IDX_0_VAL_sense 0
#define DT_N_S_ictl_800d0000_IRQ_IDX_0_VAL_sense 0
#define DT_N_S_ictl_800e0000_IRQ_IDX_0_VAL_sense 0
#define DT_N_S_ictl_800f0000_IRQ_IDX_0_VAL_sense 0

#endif

#endif
