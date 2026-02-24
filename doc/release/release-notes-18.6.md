# v18.6.0

We are pleased to announce the release of TT System Firmware firmware version 18.6.0 🥳🎉.

Major enhancements with this release include:

## Zephyr-4.2.0-rc1

This release of `tt-system-firmware` migrates our application to run on v4.2.0-rc1 of the Zephyr
Real-time Operating System. While the final v4.2.0 release of Zephyr is not yet available, we are
committed to ensuring that our platform will run on it when it is available.

Tenstorrent's usage of Zephyr API usage has remained fairly consistent over the last release
cycle. Of course, Zephyr's APIs have been very stable for some time and the upstream continuous
test environment is quite thorough.

Upstream (draft) release notes for the up-coming v4.2.0 release of Zephyr are available
[here](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/releases/release-notes-4.2.rst).

More information about the Zephyr Release Process is available
[here](https://docs.zephyrproject.org/latest/project/release_process.html).

## New and Experimental Features

* Enabled PCIe event counters

## Performance Improvements

* Enabled Quad DDR SPI mode to speed up loading cmfw

## Stability Improvements

* Aligned ASIC location definition in the SPI table with that of the telemetry table
* Update Blackhole ERISC FW to v1.4.2
  * Updated ASIC location definition to align with SPI table changes
* Implemented SDIF timeout for PVT sensor read
* Update Blackhole MRISC FW to v2.8
  * Added Tuning setting for P300B cards
  * * dram_ocd_pulldown_offset = 3 (MR2[2:0]) (increase pull down driver strength)
  * * dram_data_termination_offset = 1 (MR3[2:0]) (decrease DRAM termination)

* Wormhole FW blob updated
  * CMFW 2.34.0.0
    * Update voltage regulator settings for n300
    * Additional verification of voltage regulator programming
  * ERISC FW 6.7.0.0
    * Add multi-mesh support for T3K
    * Fix intermittent static training synchronization failures

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

## Drivers

* add `tenstorrent,bh-gpio` driver in place of `bh_arc` GPIO library
* add `maxim,max6639` driver + tests
* add `tenstorrent,bh-fwtable` driver + tests

* add Zephyr PLL driver + tests

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

An overview of required and recommended changes to make when migrating from the previous v18.5.0 release can be found in [v18.6.0 Migration Guide](https://github.com/tenstorrent/tt-system-firmware/tree/main/doc/release/migration-guide-18.6.md).

## Full ChangeLog

The full ChangeLog from the previous v18.5.0 release can be found at the link below.

https://github.com/tenstorrent/tt-system-firmware/compare/v18.5.0...v18.6.0
