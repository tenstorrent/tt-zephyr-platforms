# v19.4.0

> This is a working draft for the up-coming v19.4.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.4.0 🥳🎉.

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
### Stability Improvements

* Update Wormhole FW blob
  * ERISC FW 6.7.3.0
    * Fix retrain hangs with Active Cables
    * Adjust DFE value to improve BER on WH UBB QSFP ports
    * Link quality improvements to non-retimer long trace on WH UBB
    * Updated retraining logic to set train_status to LINK_TRAIN_TRAINING when entering into retraining
    * Remove link_training_fw_phony debug feature from eth_init to free up more space
  * Switch WH FW over to Zephyr ARC toolchain 0.17.4
  * Fix BSS to be 0-initialized
    * Causes telemetry `TAG_TIMER_HEARTBEAT` to start at 0 upon reset
  * GDDR improvement for WH Galaxy
    * Increase read latency from 23 to 25 for 14G
    * Explicitly disable EDC tracking
  * Add vendor-specific GDDR settings and report GDDR vendor

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.3.0 release can be found in [v19.4 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.4.md).

## Full ChangeLog

The full ChangeLog from the previous v19.3.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.3.0...v19.4.0
