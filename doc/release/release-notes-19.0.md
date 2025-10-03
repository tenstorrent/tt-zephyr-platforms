# v19.0.0

> This is a working draft for the up-coming v19.0.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.0.0 🥳🎉.

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
<!-- Stability Improvements, if applicable -->

### Stability Improvements

* Update Blackhole ERISC FW to v1.6.1
  * Fix snapshot reading bug in eth_runtime where the upper 32 bits of a preceding metric read is picked up by the following metric read
  * Remove interrupt enablement as current implementation can cause infinte loops
  * Changed logical_eth_id calculation using new enabled_eth param to address SYS-2064
  * Added ASIC ID in chip_info and param table to address SYS-2065
  * Changed manual EQ TX-FIRs for ASIC 8 Retimer ports to address SYS-2096
  * Only trigger retraining if check_link_up polls link down for 5ms
  * Removed BIST check in training sequence, improves stability a bit
  * Send chip_info packet on retrain completion, which along with BIST disabled allows for a single chip with an active link to be reset and allow the link come back up

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.12.0 release can be found in [v19.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.0.md).

## Full ChangeLog

The full ChangeLog from the previous v18.12.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.12.0...v19.0.0
