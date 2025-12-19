# v19.4.1

## What's Changed

### Bug Fixes

* Update Wormhole FW blob to revert ERISC FW to 6.7.2.0
  * Due to ETH training instability on WH Galaxy with ERISC FW 6.7.3.0

# v19.4.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.4.0 🥳🎉.

Major enhancements with this release include:

## What's Changed

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
* Re-release MRISC FW 2.11
  * Reduce Galaxy datarate to 14G to address regression in FW bundle v19.3.0
  * Reapply memory bandwidth utilization improvements to all board types

### Drivers

* Fix SMBus cancel/uncancel interface
  * Driver-specific implementations are now properly called
  * Cancel state is now properly taken into account when starting a transaction, which fixes some PCIe enumeration issues

### Libraries

* BH ARC library improvements
  * AICLK power management enhancements
    * Apply AICLK busy state from GO_BUSY and POWER messages for legacy application compatibility
    * Track last BUSY/IDLE message received and apply AICLK state based on both that and power settings
    * Extended native simulation support for aiclk_ppm initialization
  * Message queue improvements
    * Add doxygen documentation and structured access for ASIC state messages (TT_SMC_MSG_ASIC_STATE0 and TT_SMC_MSG_ASIC_STATE3)
    * Add doxygen documentation and structured access for TT_SMC_MSG_TEST
    * Code formatting improvements (clang-format)
  * Power management logging improvements
    * Condense noisy prints in bh_power into a single print statement

### Debug / Developer Features

* Scripts and tooling improvements
  * `tt_bootstrap`: Add support for erasing flash with `west flash -r tt_bootstrap --erase`
  * `vuart`: Open file descriptor with O_APPEND flag to prevent power-on when opening console
  * Add `update_versions.sh` script to upgrade versions of SMC, DMC, and FW during the release process


### Other Notable Changes

* Documentation
  * Update release process documentation to use version update script
  * Add firmware signing key conflicts guide explaining how to move from a production-signed firmware to a development-signed firmware (v19.0.0+)

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
