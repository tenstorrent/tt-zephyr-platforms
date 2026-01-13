# v19.5.0

> This is a working draft for the up-coming 19.5.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.5.0 🥳🎉.

Major enhancements with this release include:

## What's Changed

### P150 Harvesting Configuration

New P150x cards will be shipped with Bin 3 parts, which lack 2 functional
tensix columns. To present a unified interface to metal and other system
software, firmware v19.5 and later now disable 2 tensix columns on P150x cards.

Users who wish to retain all columns in their functional state may run
`scripts/update_tensix_disable_count.py` against a firmware bundle before
flashing it, but should be aware this is not a supported configuration

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
<!-- Stability Improvements, if applicable -->
<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.4.0 release can be found in [19.5 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.5.md).

## Full ChangeLog

The full ChangeLog from the previous v19.4.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.4.0...v19.5.0
