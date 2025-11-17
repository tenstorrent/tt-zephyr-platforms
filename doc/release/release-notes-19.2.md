# v19.2.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.2.0 🥳🎉.

Major enhancements with this release include upgrading to Zephyr 4.3.0, delivering configurable PCIe BAR support via the new `CntlInitV2()` flow, and introducing a standalone `tt_fwbundle` tooling suite.

## What's Changed

### Platform & Dependencies
- **Zephyr Upgrade**: Bumped the Zephyr manifest to the v4.3.0 release to pick up the latest upstream fixes and features.
- **Devicetree Cleanups**: Relocated Blackhole SMC DTSI content into the vendor tree for clearer layering.

### Drivers
- **MSPI Upstreaming**: Migrated the MSPI controller and MSPI NOR flash drivers to their upstream Zephyr counterparts, reducing local maintenance.
- **Clock Control Emulation**: Added an emulated clock control driver and bindings used by native-sim tests.
- **DMA Reliability**: Switched the Blackhole DMA driver to RAM-to-Tensix transfers and enforced DT alignment so NOC DMA buffers meet 64-byte requirements.

### Libraries & Firmware
- **Power Management**: Refactored `bh_arc` power handling into `bh_power`, added L2 CPU enable/disable plumbing, and exposed the control through `tt_shell` with stricter error handling.
- **PCIe Enhancements**: Adopted the new `CntlInitV2()` API, added structured logging, and aligned MSI message buffers for cleaner PCIe initialization.
- **Firmware Tables**: Populated PCIe BAR size metadata across Blackhole board firmware tables to accompany the new BAR masks.

### Tooling & Tests
- **Firmware Bundle Utility**: Introduced the `tt_fwbundle` script with create, combine, list, diff, and extract commands, replacing the legacy `tt_boot_fs` bundle helpers.
- **Automated Coverage**: Added unit tests for the new firmware bundle flows and expanded native-sim PCIe MSI coverage.

### Applications & Stability
- **DMC I2C Timeout**: Tuned the STM32 I2C transfer timeout to track upstream changes and avoid spurious stalls.
- **Data Path Alignment**: Ensured Tensix RAM buffers are 64-byte aligned to match the NOC DMA engine’s requirements.

### Continuous Integration
- **Devicetree Linting**: Enabled the devicetree linter in compliance checks and regenerated DTS files accordingly.
- **Workflow Hardening**: Updated CI runners and tag fetching to improve build reliability on shared hardware.

### Documentation
- **Getting Started**: Documented Python 3.12 installation steps for development environments.

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.1.0 release can be found in [v19.2 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.2.md).

## Full ChangeLog

The full ChangeLog from the previous v19.1.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.1.0...v19.2.0