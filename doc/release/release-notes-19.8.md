# v19.8.0

> This is a working draft for the up-coming 19.8.0 release.

We are pleased to announce the release of TT System Firmware firmware version 19.8.0 🥳🎉.

Major enhancements with this release include:

## Wormhole

### Bug Fixes
- Fix GDDR speed reporting in tt-smi (regression introduced in 19.5.1 / 19.6.0)

### Power & Performance Improvements
- Add support for more idle power saving features
  - GDDR low power mode
  - PCIe switch to Gen1
  - Lower vcore voltage when in aiclk idle

## Blackhole

<!-- Subsections can break down improvements by (area or board) -->
<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- Performance Improvements, if applicable -->
<!-- New and Experimental Features, if applicable -->
### New Features

- Added cable fault detection to protect the board when the 12V-2x6 power cable is missing or improperly installed. The DMC detects the cable power limit via GPIO sense pins and writes it to SCRATCH_1 before ARC soft reset. If the SMC reads a power limit of 0, it enters a low-power cable fault mode: all tiles are clock-gated, firmware init is skipped for affected subsystems, and PCIe communication with the host is maintained.

<!-- External Project Collaboration Efforts, if applicable -->
### Stability Improvements

- Updated Blackhole ERISC FW to v1.10.1, change list from v1.9.0:
  - v1.10.1
    - Fix retraining sequence in LT only mode caused by missing register clear
  - v1.10.0
    - Improve link stability and training reliability for ANLT mode (LT mode)
      - Added LT only training mode without AN, now default over ANLT
    - Fixed bug in Manual EQ sequence that caused reinits to hang
    - Updated ETH mailbox message selection
      - Removed INTERRUPT, CABLE_CHECK, TELEMETRY_EVENT, REMOTE_ALIVE, PORT_MODE messages as they were deemed unneeded
      - Did not re-number messages to avoid updates in higher layer software
- Updated Blackhole MRISC FW to v2.14
  - Fix training instability in revB UBBs
  - Clear EDC error counters after training so they don't get reported in telemetry

### Power Management

- Re-enabled PHY powerdown for MRISC on Galaxy systems

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.7.0 release can be found in [19.8 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.8.md).

## Full ChangeLog

The full ChangeLog from the previous v19.7.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.7.0...v19.8.0
