# v19.5.0

> This is a working draft for the up-coming 19.5.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.5.0 🥳🎉.

Major enhancements with this release include:

## Blackhole Changes

### p150 Tensix Core Count

Beginning January 2026, all Blackhole p150 accelerator cards (p150a, p150b) will ship with **120 Tensix cores** instead of 140. To present a unified interface to metal and other system software, firmware v19.5.0 and later will change the core count on all existing cards to 120.  Typical workloads show a non-material (~1–2%) performance difference.
You may observe a change in grid size in metal, which may require updates to applications that depend on grid layout.

## Wormhole Changes

### Stability Improvements

- Update ERISC FW to 7.5.0, change list from 7.2.0:
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
