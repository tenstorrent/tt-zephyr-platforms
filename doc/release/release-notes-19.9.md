# v19.9.0

> This is a working draft for the up-coming 19.9.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.9.0 🥳🎉.

Major enhancements with this release include:

## What's Changed

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

### New API

- Added [`TT_SMC_MSG_TOGGLE_SINGLE_TENSIX_RESET`](https://docs.tenstorrent.com/tt-system-firmware/doxygen/structtoggle__single__tensix__reset__rqst.html) ARC message to reset a specific Tensix tile (SYS-2832).

### Stability Improvements

- Updated Blackhole MRISC FW to v2.15
  - Tuned CA/WCK settings to address Galaxy revC data mismatch issues
- Fixed a bug with specific chips where the bootrom would get stuck on memory repair (postcode 0x13) (SYS-2903).

### Bug Fixes

- Fixed red LED not staying on for manufacturing test (SYS-3844).

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.8.0 release can be found in [19.9 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-19.9.md).

## Full ChangeLog

The full ChangeLog from the previous v19.8.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v19.8.0...v19.9.0
