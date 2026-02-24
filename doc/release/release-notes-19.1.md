# v19.1.0


We are pleased to announce the release of TT System Firmware firmware version 19.1.0 🥳🎉.

Major enhancements with this release include new DMA drivers and enhanced SMBus communication stability.

## What's Changed

### Stability Improvements

  - Fixed a timing issue in Zephyr's `snps,designware-i2c` driver that caused intermittent loss of packet data
  - Improved error rate from 1 in 10k to 0 in 2M

### Drivers

- **New DMA Support**: Added comprehensive DMA driver infrastructure
  - New `dma_arc_hs` driver implementation
  - NOC-to-NOC DMA driver with proper device tree bindings
  - Full upstream DMA test configuration and support
  - Removed deprecated `noc_dma` files and includes

### Libraries

- **SMBus Enhancements**:
  - Improved DMC ping stability with better error handling
  - Added legacy ping command support for backwards compatibility

- **DMA Library Migration**:
  - Replaced custom NOC DMA implementation with standard Zephyr DMA subsystem

- **Removed APIs**:
  - Removed deprecated `tenstorrent fwupdate` library

### Documentation

- **Architecture Documentation**: Added comprehensive documentation about the boot process for both SMC and DMC components
- **Testing Documentation**: Updated pytest documentation with better usage examples and requirements

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.0.0 release can be found in [v19.1 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.1.md).

## Full ChangeLog

The full ChangeLog from the previous v19.0.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.0.0...v19.1.0
