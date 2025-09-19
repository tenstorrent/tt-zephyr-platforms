# v18.4.0

We are pleased to announce the release of TT Zephyr Platforms firmware version 18.4.0 🥳🎉.

Major enhancements with this release include:

<!-- H3 Performance Improvements, if applicable -->
<!-- H3 New and Experimental Features, if applicable -->
<!-- H3 External Project Collaboration Efforts, if applicable -->
<!-- H3 Stability Improvements, if applicable -->

## New Features

* DMC now increments a counter for thermal trips and reports the count to SMC
  * SMC now reports this value in the telemetry table
  * This counter is reset on PERST
* Add an SMC message to toggle Tensix resets for testing purposes

## Performance Improvements

* Wormhole FW blob updated
  * SPI bootrom 3.13.0.0
    * Remove PCIe MPS limit (**Note: tt-kmd >= 1.33 is required**)
  * CMFW 2.33.0.0
    * Fix to decrease variation across TMONs at idle
    * Make therm trip limit a SPI parameter
    * Backport BH-style telemetry tables
  * ERISC FW 6.6.15.0
    * Training improvements for 6U UBB Galaxy

<!-- H1 Security vulnerabilities fixed? -->

<!-- H3 Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- H2 New Samples, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- H2 Other Notable Changes, if applicable -->

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->
<!-- UL Drivers -->
<!-- UL Libraries -->

<!-- H2 New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.3.0 release can be found in [v18.4.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-v18.4.0.md).

## Full ChangeLog

The full ChangeLog from the previous v18.3.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.3.0...v18.4.0
