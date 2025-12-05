# v19.3.1

## What's Changed

### Bug Fixes

* Reverted MRISC version due to regression on Blackhole Galaxy.

# v19.3.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.3.0 🥳🎉.

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

### New and Experimental Features

* Update Blackhole ERISC FW to v1.7.0
  * ETH msg DYNAMIC_NOC_INIT: Initialize dynamic NOC state for the ethernet core
* Doppler Power Management
  * Low-latency power management optimizing bursty workloads on p100a and p150* boards
  * 100 kHz tick rate, switched to a tick-less kernel
* Support BAR4 resizing
* New ARC message for blinking the board fault LED for board identification purposes
* New firmware bundle for Debian
* Improved GDDR bandwidth utilization

### Drivers

* ``snps,designware-dma-arc-hs``: integrated ARC DMA driver into codebase
  * ARC DMA driver follows the upstream Zephyr DMA driver API

<!-- External Project Collaboration Efforts, if applicable -->

### Stability Improvements

* Update Blackhole ERISC FW to v1.7.1
  * Enable all TX/RXQ to be in packet mode after initial training for SYS-2294
  * Implement NOC counters in L1 for SYS-2489
  * Fixed left vs right chip logic in manual EQ override for P300
  * Changed default training mode back to ANLT for P150s and P300 on-PCB traces
  * Fixed bug that clears live status counters unnecessarily
  * Removed BIST check altogether from SerDes init sequence
  * Minimized SerDes access during retraining to reduce NOC traffic
  * Improve one sided reset and retrain logic for ANLT
  * Reduced ETH training timeouts
* Fixed a potential hang when Tensix is generating NOC traffic to idle GDDR
* Added more tests for ARC and I2C messaging
* Made tensix idle before clock gating in our reset flows
  * More graceful cleanup in cases of crashes, and more robust power down flows

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.2.0 release can be found in [v19.3 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.3.md).

## Full ChangeLog

The full ChangeLog from the previous v19.2.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.2.0...v19.3.0