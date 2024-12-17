#ifndef ETH_H
#define ETH_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <arc_dma.h>

#define MAX_ETH_INSTANCES 14

int LoadEthFw(uint32_t eth_inst, uint32_t ring, uint8_t* fw_image, uint32_t fw_size);
int LoadEthFwCfg(uint32_t eth_inst, uint32_t ring, uint8_t* fw_cfg_image, uint32_t fw_cfg_size);

void ReleaseEthReset(uint32_t eth_inst, uint32_t ring);

#endif
