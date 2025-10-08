/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef INCLUDE_TENSTORRENT_LIB_BH_ARC_PRIV_H_
#define INCLUDE_TENSTORRENT_LIB_BH_ARC_PRIV_H_

#include <tenstorrent/bh_arc.h>

int bharc_enable_i2cbus(const struct bh_arc *dev);
int bharc_disable_i2cbus(const struct bh_arc *dev);

#endif
