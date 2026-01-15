# v18.12.2

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.12.2 🥳🎉.

This is a point release to enable recovery firmware (which is based on 18.12) to
operate on newer P300 cards

## Major Highlights

* Patch to STM32 SPI flash loader algorithms to support latest P300 board revision
* Multiple CI improvements to enable executing CI on new runner configurations

# v18.12.1

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.12.1 🥳🎉.

This is a point release to ensure that users are able to test with the latest ERISC firmware while
v19.0 is going through additional testing.

## Major Highlights

* Update Blackhole ERISC FW to v1.7.0
  * ETH msg PORT_RETRAIN: force a link to retrain
  * ETH msg PORT_REINIT: asks a failed port to redo initialization
  * ETH msg PORT_LOOPBACK: allows putting the port in internal or external loopback
  * ETH msg INTERRUPT: enables or disables interrupts to the ERISC
  * ETH msg PORT_ACTION: force the link to be up or down via the MAC
  * ETH msg CABLE_CHECK: checks whether a cables exists or not
  * ETH msg TELEMETRY_EVENT: handles specific telemetry exchange events over the link
  * ETH msg REMOTE_ALIVE: send packet to check if remote side is alive
  * ETH msg PORT_SPEED: re-initializes the port to a different speed

## Stability Improvements

* ERISC FW v1.7.0
  * Fix snapshot reading bug in eth_runtime where the upper 32 bits of a preceding metric read is picked up by the following metric read
  * Remove interrupt enablement as current implementation can cause infinte loops
  * Changed logical_eth_id calculation using new enabled_eth param to address SYS-2064
  * Added ASIC ID in chip_info and param table to address SYS-2065
  * Changed manual EQ TX-FIRs for ASIC 8 Retimer ports to address SYS-2096
  * Only trigger retraining if check_link_up polls link down for 5ms
  * Removed BIST check in training sequence, improves stability a bit
  * Send chip_info packet on retrain completion, which along with BIST disabled allows for a single chip with an active link to be reset and allow the link come back up
  * Set manual TX FIR parameters for warp cable connections on P300 to 1/3/4/45/2 for PCB-1997
  * increase stack size to 2048 for SYS-2266
  * inline icache flush function for SYS-2267
  * Fix for reset skew where one tt-smi reset should make other side up
  * Added interrupt enablement again, controlled via INTERRUPT_CHECK feature enable flag
  * Moved auto retraining outside of link_status_check into its own link check state mechine, controlled via DYNAMIC_LINK_STATE_CHECK feature enable flag
  * Added link flap check based on resend and un-cor words
  * Added eth_reinit state machine to handle fail case when port is up
* PVT Sensor
  * correct PVT RTIO buffer size and frame count for decode

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
