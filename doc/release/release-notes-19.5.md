# v19.5.0

We are pleased to announce the release of TT System Firmware firmware version 19.5.0 🥳🎉.

Major enhancements with this release include:

## Wormhole Changes

### Stability Improvements

- Update Wormhole ERISC FW to 7.5.0, change list from 7.2.0:
  - 7.5.0
    - Adjust the resend chip info packet time to resolve the link training failures with ANLT ports
    - Minimize static training retries to compensate for longer delays in the training sequence
    - Adjust DFE settings for weak lanes only to improve the link stability
  - 7.4.0
    - Code clean-up and documentation
    - Use register defines and convert arrays into data structures
  - 7.3.0
    - Fix retrain hangs with Active Cables
    - Adjust DFE value to improve BER on WH UBB QSFP ports
    - Link quality improvements to non-retimer long trace on WH UBB
    - Updated retraining logic to set train_status to LINK_TRAIN_TRAINING when entering into retraining
    - Remove link_training_fw_phony debug feature from eth_init to free up more space

## Blackhole Changes

### p150 Tensix Core Count

Beginning January 2026, all Blackhole p150 accelerator cards (p150a, p150b) will ship with **120 Tensix cores** instead of 140. To present a unified interface to metal and other system software, firmware v19.5.0 and later will change the core count on all existing cards to 120.  Typical workloads show a non-material (~1–2%) performance difference.
You may observe a change in grid size in metal, which may require updates to applications that depend on grid layout.

### Stability Improvements

- Fixed boardcfg loading error handling in recovery image
- Updated Blackhole ERISC FW to v1.8.1, change list from v1.7.1:
  - v1.8.1 (2025-01-23)
    - Change P150 products to use Manual EQ training mode
    - Updated MACPCS init sequence to wait longer before SerDes reinit
    - Added wait for LCPLL lock check in SerDes init before training start
    - Fixed bug where calling eth_mac_pcs_reinit with option = 1 would immediately return without running the function
    - Increased AN timeout and disabled SerDes lt_timeout_enable to allow for better control of the ANLT sequence via FW
  - v1.8.0 (2026-01-20)
    - Updated TX/RX remapping for P300 and P150
    - Updated settings to improve PRBS test mode
      - Keep TX BIST enabled even on Serdes training fail
      - Disable FW features at beginning of initialization for PRBS tests
    - Restore some of the timeout changes to pre v1.7.1 settings as new timeouts proved to be problematic on galaxy systems
    - Cleaned up RX retraining sequence for Manual EQ
    - Added tx_barrier to sync TX/RX across multiple eth controllers on init
    - Added eth_link_recovery to eth_api_table to allow access to runtime FW
    - Added disable_l1_cache function to disable data cache completely, function called in main() of base FW and all test FWs
    - Ensure eth_api_table is not wiped between inits
    - Changed some ETH messages:
      - ETH msg PORT_REINIT -> PORT_REINIT_MACPCS: Initiates fill initialization sequence based on selected option
      - ETH msg PORT_RETRAIN -> PORT_REINIT_SERDES: Only initiates serdes initalization sequence based on selected option

### Power Management

- Galaxy TDP limit reduced from 170 W to 130 W to stay within system power limits
- Disabled PHY powerdown for MRISC on Galaxy systems to address DRAM funtest regression

### Telemetry & Monitoring

- Added telemetry entries for enabled AICLK min/max arbiters (`TAG_ENABLED_MIN_ARB` and `TAG_ENABLED_MAX_ARB`)
- Added telemetry for effective AICLK min/max arbiter values (`TAG_AICLK_ARB_MIN` and `TAG_AICLK_ARB_MAX`)
- Added Doxygen documentation for `aiclk_arb_max` and `aiclk_arb_min` enumerations
- PVT sensor message fixes
  - Revert backward-incompatible message interface changes to `READ_{TS,PD,VM}` introduced in v19.1.0
  - Fixed conversion truncation issue in voltage monitoring messages
  - Improved error checking in PVT functions
- Added tracing for AICLK updates (disabled by default)

### Drivers

- Added interrupt support to ARC DMA driver and enabled it by default
- Added "is alive" check to PVT sensor drivers

### Tooling

- Add support for newer P300 boards to blackhole recovery script
- Properly support flash read across multiple blocks with pyocd
- tt-tracing improvements
  - Added `-b` command-line argument to select BAR (bar0 or bar4)
  - Improved tracing performance when running in parallel with `tt-burnin`
  - Fixed to send enable tracing command only once (not on every loop iteration)
  - Updated documentation with coding examples for custom tracing events
  - Fixed babeltrace2 version to 2.0.4 for compatibility

### Build System & Dependencies

- Migrated from patch-based workflow to Zephyr fork
- Removed all usages of `west patch` treewide
- Fixed git describe to use correct module directory (enables correct version tags for applications built in different repositories)

## Grendel Changes

- Initial support added for the Grendel SMC platform
  - 4-core Berkeley Rocketcore with peripheral complement for external device interfacing
- Initial devicetree definition
- Virtual console driver for simulation debugging via printf
- Basic build workflow for CI validation

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.4.0 release can be found in [19.5 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.5.md).

## Full ChangeLog

The full ChangeLog from the previous v19.4.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.4.0...v19.5.0
