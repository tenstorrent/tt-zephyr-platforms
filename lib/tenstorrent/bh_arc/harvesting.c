#include <zephyr/sys/util.h>
#include "harvesting.h"
#include "fw_table.h"

TileEnable tile_enable;

void CalculateHarvesting() {
  // These are just the default values, taking some SPI parameters into account
  // This function needs to be completed with fuse reading and additional SPI parameters

  // Initial values, everything enabled
  tile_enable.tensix_col_enabled = BIT_MASK(14);
  tile_enable.eth_enabled = BIT_MASK(14);
  tile_enable.eth5_serdes = true;
  tile_enable.eth8_serdes = true;
  tile_enable.gddr_enabled = BIT_MASK(8);
  tile_enable.l2cpu_enabled = BIT_MASK(4);
  tile_enable.eth_serdes_connected = BIT_MASK(12);

  // Eth handling
  // Only enable 2 of 3 in eth {4,5,6}
  if (FIELD_GET(GENMASK(6,4), tile_enable.eth_enabled) == BIT_MASK(3)) {
    tile_enable.eth_enabled &= ~BIT(6);
  }
  // Only enable 2 of 3 in eth {7,8,9}
  if (FIELD_GET(GENMASK(9,7), tile_enable.eth_enabled) == BIT_MASK(3)) {
    tile_enable.eth_enabled &= ~BIT(9);
  }
  if (get_fw_table()->eth_property_table.eth_disable_mask_en) {
    tile_enable.eth_enabled &= ~(get_fw_table()->eth_property_table.eth_disable_mask);
  }

  // PCIe and SERDES handling
  tile_enable.pcie_usage[0] = get_fw_table()->pci0_property_table.pcie_mode;
  if (tile_enable.pcie_usage[0] == FwTable_PciPropertyTable_PcieMode_DISABLED) {
    tile_enable.pcie_num_serdes[0] = 0;
  } else {
    tile_enable.pcie_num_serdes[0] = MIN(get_fw_table()->pci0_property_table.num_serdes, 2);
    if (tile_enable.pcie_num_serdes[0] == 1) {
      tile_enable.eth_serdes_connected &= ~(BIT(0)|BIT(1));
    } else if (tile_enable.pcie_num_serdes[0] == 2) {
      tile_enable.eth_serdes_connected &= ~(BIT(0)|BIT(1)|BIT(2)|BIT(3));
    }
  }
  tile_enable.pcie_usage[1] = get_fw_table()->pci1_property_table.pcie_mode;
  if (tile_enable.pcie_usage[1] == FwTable_PciPropertyTable_PcieMode_DISABLED) {
    tile_enable.pcie_num_serdes[1] = 0;
  } else {
    tile_enable.pcie_num_serdes[1] = MIN(get_fw_table()->pci1_property_table.num_serdes, 2);
    if (tile_enable.pcie_num_serdes[1] == 1) {
      tile_enable.eth_serdes_connected &= ~(BIT(11)|BIT(10));
    } else if (tile_enable.pcie_num_serdes[1] == 2) {
      tile_enable.eth_serdes_connected &= ~(BIT(11)|BIT(10)|BIT(9)|BIT(8));
    }
  }
}