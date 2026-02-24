# v18.5.0

We are pleased to announce the release of TT System Firmware firmware version 18.5.0 🥳🎉.

Major enhancements with this release include:

<!-- H3 Performance Improvements, if applicable -->
<!-- H3 New and Experimental Features, if applicable -->
<!-- H3 External Project Collaboration Efforts, if applicable -->

## Stability Improvements

* Update Blackhole ERISC FW to v1.4.1
  * Fixed bug in FW where training would stall when enabling training on P300 ports that do not connect
    outside of the Chip at all
* Prevent invalid overwrite of DMC init time
* Automatically recover firmware after hardware CI jobs
* fan_ctrl: disable initial fan spin-up to 100%
* pcie: drive perst of cem1 slot when operating in RC mode

<!-- H1 Security vulnerabilities fixed? -->

<!-- H2 API Changes -->

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

<!-- UL PCIe -->
<!-- UL DDR -->
<!-- UL Ethernet -->
<!-- UL Telemetry -->
<!-- UL Debug / Developer Features -->

## Drivers

* renamed `tenstorrent,blackhole-reset` to `tenstorrent,bh-reset` to be consistent with other compatibles
* add `tenstorrent,bh-watchdog` driver + tests
* add `tenstorrent,bh-gpio` driver + tests

<!-- UL Libraries -->

## Documentation

* added beginning of Sphinx documentation generation
* updated board doc for `tt_blackhole`

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.4.0 release can be found in [v18.5.0 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-v18.5.0.md).

### Full ChangeLog

The full ChangeLog from the previous v18.4.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v18.4.0...v18.5.0
