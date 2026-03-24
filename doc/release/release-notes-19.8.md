# v19.8.0

> This is a working draft for the up-coming 19.8.0 release.

We are pleased to announce the release of TT System Firmware firmware version 19.8.0 🥳🎉.

Major enhancements with this release include:

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
