/*
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TENSTORRENT_SYS_INIT_DEFINES_H_
#define TENSTORRENT_SYS_INIT_DEFINES_H_

#include <zephyr/init.h>

/* SYS_INIT APPLICATION defines */
#define register_interrupt_handlers_PRIO      0
#define arc_dma_init_PRIO                     1
#define InitSpiFS_PRIO                        2
#define bh_arc_init_start_PRIO                3
#define CATEarlyInit_PRIO                     4
#define CalculateHarvesting_PRIO              5
#define DeassertTileResets_PRIO               6
#define PLLInit_PRIO                          7
#define PVTInit_PRIO                          8
#define NocInit_PRIO                          9
#define AssertSoftResets_PRIO                 10
#define DeassertRiscvResets_PRIO              11
#define InitAiclkPPM_PRIO                     12
#define pcie_init_PRIO                        13
#define wipe_l1_PRIO                          14
#define InitMrisc_PRIO                        15
#define eth_init_PRIO                         16
#define InitSmbusTarget_PRIO                  17
#define regulator_init_PRIO                   18
#define avs_init_PRIO                         19
#define tensix_cg_init_PRIO                   20
#define InitNocTranslationFromHarvesting_PRIO 21
#define gddr_training_PRIO                    22
#define CATInit_PRIO                          23
#define bh_arc_init_end_PRIO                  24

#define SYS_INIT_APP(func) SYS_INIT(func, APPLICATION, func##_PRIO)

#endif
