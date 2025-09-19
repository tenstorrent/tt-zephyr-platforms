# v18.3.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.3.0 🥳🎉.

Major enhancements with this release include:

<!-- H3 Performance Improvements, if applicable -->
<!-- H3 New and Experimental Features, if applicable -->
<!-- H3 External Project Collaboration Efforts, if applicable -->
<!-- H3 Stability Improvements, if applicable -->

## New Features

* DMC now reads and sends power (instead of current) from INA228 device to SMC
  * SMC now uses power reading as input to Total Board Power (TBP) throttler instead of `12 * current`
* DMC support for accessing tca9554a GPIO expanders added

## Stability Improvements

* Add I2C handshake between SMC and DMC FW to ensure that initialization messages are received
* Total Board Power (TBP) throttler parameters have been tuned, and TBP limit is now set in the fwtable to guarantee product definition is followed

<!-- H1 Security vulnerabilities fixed? -->

<!-- H3 Removed APIs, if applicable -->

<!-- same order for Subsequent H2 sections -->
<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

## Removed APIs
* Telemetry no longer reports TAG_INPUT_CURRENT

<!-- H3 Deprecated APIs, if applicable -->

<!-- H3 New APIs, if applicable -->
## New APIs
* Telemetry now reports TAG_INPUT_POWER to replace TAG_INPUT_CURRENT

<!-- H2 New Boards, if applicable -->

<!-- H2 New Samples, if applicable -->

<!-- H2 Other Notable Changes, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.2.0 release can be found in [v18.3.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-v18.3.0.md).
