/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _IRQNUM_H_
#define _IRQNUM_H_

/* List of ARC IRQ numbers that aren't captured in Device Tree */
#define IRQNUM_ARC_MISC_CNTL_IRQ0 32
#define IRQNUM_PCIE0_ERR_INTR     47
#define IRQNUM_PCIE1_ERR_INTR     54

#define IRQNUM_MSI_CATCHER_NONEMPTY 57
#define IRQNUM_MSI_CATCHER_OVERFLOW 58

#endif
