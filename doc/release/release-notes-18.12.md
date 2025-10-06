# v18.12.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.12.0 🥳🎉.

This release brings significant enhancements to power management, SMBus communication robustness, and developer tooling, along with improved stability across the platform.

## Major Highlights

- **Enhanced Power Management**: New MRISC PHY powerdown/wakeup support and power command capabilities
- **Improved SMBus Reliability**: Enhanced error detection, logging, and robustness for CM2DM operations
- **Advanced Developer Tooling**: New stack dump utilities and improved console tooling
- **Zephyr v4.3.0 Alignment**: Updated to latest Zephyr pre-release for better upstream compatibility
- **Streamlined Board Support**: Focused support on production devices

## New Features

### Power Management
- **lib: bh_arc**: Added support for MRISC PHY powerdown/wakeup operations
- **lib: bh_arc**: Implemented new Power command functionality
- **boards: tt_blackhole**: Enabled AICLK PPM in firmware table for Galaxy boards
- **boards: tenstorrent**: Applied more conservative TDP limits for P300 boards

### Communication & Protocols
- **app: dmc**: Added support for SMBus block process call (pcall) operations
- **drivers: smbus: stm32**: Enhanced with block read capabilities
- **app: smc**: Added overlay configuration to build DMFW with I2C shell enabled

### Developer & Debug Tools
- **scripts**: New `dump_smc_stack.py` utility for on-demand SMC state analysis
- **scripts: tooling**: Enhanced `tt-console` with PCIe rescan skip option for smooth TT-KMD transitions (2.3.0 to 2.4.1)
- **scripts**: Updated `blackhole_recovery` with improved status messaging and increased delays

## Stability & Robustness Improvements

### Communication Reliability
- **CM2DM Protocol**: Enhanced robustness with detection of duplicate SMBus messages and improved error handling
- **lib: tenstorrent: bh_arc**: Improved error logging in SPI EEPROM operations

### System Reliability
- **app: dmc**: Implemented fine-grained DMC events, replacing the previous single "wakeup" flag approach
- **regulators**: Fixed Galaxy SerDes programming regression
- **boards: tenstorrent**: Reduced P300 I2C clock speeds to minimize packet errors
- **lib: bh_chip**: Optimized I2C gate handling during BH ARC reset operations

### Infrastructure
- **CI/CD**: Migrated to centralized Docker containers hosted at `ghcr.io/tenstorrent`
- **scripts: tooling**: Reworked logging macros with new rate-limited variants

### Wormhole Updates
- **blobs**: Updated Wormhole firmware blob
  - CMFW 2.36.0.0
    - Attempt to enter A3 state on both chips of n300 before triggering
      reset
    - Fix VR addresses for Nebula CB P0V8_GDDR_VDD and P1V35_MVDDQ
    - Update Nebula CB SPI parameters
      - Disable DRAM training at boot
      - Voltage margin = 0
      - P0V8_GDDR_VDD override to 850 mV
      - P1V35_MVDDQ override to 1.35 V
      - Enable additional DRAM training parameters
  - ERISC 6.7.1.0
    - Enable partial response phase detector mode to compensate jitter lanes
    - Initial support for per asic resetting in WH Galaxy

## API Changes

### Breaking Changes
- **lib: bh_arc**: Renamed `msg_type` to `tt_smc_msg`
  - Header file changed from `tenstorrent/msg_type.h` to `tenstorrent/smc_msg.h`
  - **Migration Required**: Update include statements in existing code

### Platform Updates
- **zephyr: patches**: Added patch for SPI-NOR erase-block-size property support

## Zephyr Integration

### Version Update
- **manifest**: Updated to Zephyr v4.3.0-pre
  - Snapshot from Zephyr mainline prior to v4.3.0 feature freeze (October 24th)
  - Minimized in-tree patches through active upstream contribution

### Upstream Contributions
- Enhanced PWM API test suite with cleaner platform-specific conditionals
- Fixed I2C DesignWare target implementation
- Improved SMBus STM32 driver with proper block write byte count handling
- Added IRQ lock protection for CTF tracing timestamp generation
- Extended interrupt controller support for multiple DesignWare instances
- Implemented "safe" API variants for `k_event_wait()` in kernel events

## Documentation Improvements

### Enhanced Documentation
- **lib: bh_arc**: Comprehensive Doxygen documentation for host-to-SMC message types
- **doxygen**: Added supported boards reference links
- **doc**: New contribution and coding guidelines
- **docs**: Added CI job status visualization on project landing page

### Developer Guidance
- **doc**: Added code update workflow snippet: `west patch clean; west update; west patch apply`
- **lib: bh_arc**: Documented unused commands for better API clarity
- **doc**: Improved version handling using project settings from `conf.py`

## Board Support Changes

### Removed Support
- **Removed P100 (Scrappy) Support**:
  - P100 was an internal engineering unit used during initial bringup and testing
  - Support removed to focus resources on consumer-facing devices
  - **Migration Impact**: P100 users must transition to supported production boards

## Migration Guide

For detailed migration instructions from v18.11.0, including required code changes and recommended updates, please refer to the [v18.12 Migration Guide](migration-guide-18.12.md).

## Full Changelog

Complete changelog from v18.11.0 is available at:
https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.11.0...v18.12.0
