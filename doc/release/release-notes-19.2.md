# v19.2.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.2.0 🥳🎉.

Major enhancements with this release include upgrading to Zephyr 4.3.0, delivering configurable PCIe BAR support via the new `CntlInitV2()` flow, and introducing a standalone `tt_fwbundle` tooling suite.

## What's Changed

### Drivers
- **Clock Control Emulation**: Added an emulated clock control driver and bindings used by native-sim tests.
- **DMA Reliability**: Switched the Blackhole DMA driver to RAM-to-Tensix transfers and enforced DT alignment so NOC DMA buffers meet 64-byte requirements.

### Libraries & Firmware
- **Power Management**: Refactored `bh_arc` power handling into `bh_power`, added L2 CPU enable/disable plumbing, and exposed the control through `tt_shell` with stricter error handling.
- **PCIe Enhancements**: Rolled out the `CntlInitV2()` path as a working demo of runtime BAR sizing, consolidating init parameters into a single struct, improving diagnostics, and aligning MSI buffers for predictable bring-up.
- **Firmware Tables**: Populated PCIe BAR size metadata across Blackhole board firmware tables to accompany the new BAR masks.

### Tooling & Tests
- **Firmware Bundle Utility**: Add `tt_fwbundle` script with create and combine commands, which replace
the fwbundle subcommand in `tt_boot_fs`. Remove this subcommand and update usage in tree to use `tt_fwbundle`.
- **Automated Coverage**: Added unit tests for the new firmware bundle flows and expanded native-sim PCIe MSI coverage.

### Applications & Stability
- **DMC I2C Timeout**: Tuned the STM32 I2C transfer timeout to track upstream changes and avoid spurious stalls.
- **Data Path Alignment**: Ensured Tensix RAM buffers are 64-byte aligned to match the NOC DMA engine’s requirements.

### Continuous Integration
- **Devicetree Linting**: Enabled the devicetree linter in compliance checks and regenerated DTS files accordingly.
- **Workflow Hardening**: Updated CI runners and tag fetching to improve build reliability on shared hardware.

### Upstream Contributions
- **DMA Test Alignment**: Carried Zephyr patches so the DMA loop, link, scatter-gather, and burst-length tests honor the `dma-buf-addr-alignment` devicetree property, matching the Blackhole NOC DMA engine’s 64-byte requirement.
- **MSPI NOR Enhancements**: Maintained an upstream-targeted patch that adds `read-frequency` binding support and enables the flash page layout API while we validate the new upstream MSPI drivers.
- **STM32 SMBus PEC Support**: Upstreamed platform-independent PEC helpers and STM32 SMBus PEC handling, allowing us to drop the large local patch while preserving CRC-backed transactions.
- **STM32 SMBus PCall Coverage**: Delivered block read/write PCall support into Zephyr’s STM32 SMBus stack to keep legacy control paths working without downstream shims.
- **I2C DesignWare Reliability**: Landed the fix that reinstates the target-mode stop callback, resolving elusive synchronization hangs observed with ARC-based controllers.
- **Patch Pruning**: Cleared out SMBus and runner patches that merged alongside the Zephyr 4.3.0 bump, shrinking our downstream maintenance surface.

### Documentation
- **Getting Started**: Documented Python 3.12 installation steps for development environments.

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.1.0 release can be found in [v19.2 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.2.md).

## Full ChangeLog

The full ChangeLog from the previous v19.1.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.1.0...v19.2.0