/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <zephyr/sys/util.h>

#define AXI_CNTL_CLEAR 0
#define AXI_CNTL_READ  BIT(31)
#define AXI_CNTL_WRITE (BIT(31) | BIT(8) | BIT_MASK(4))

#define ARC_AXI_ADDR_TDR           (2)
#define ARC_AXI_DATA_TDR           (3)
#define ARC_AXI_CONTROL_STATUS_TDR (4)
