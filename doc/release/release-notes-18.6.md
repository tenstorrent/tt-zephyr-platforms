# TT-Zephyr-Platforms v18.6.0

> [!NOTE]
> This is a working draft for the up-coming v18.6.0 release.

[comment]: <> (We are pleased to announce the release of TT Zephyr Platforms firmware version 18.6.0 🥳🎉.)

Major enhancements with this release include:

[comment]: <> (H3 External Project Collaboration Efforts, if applicable)

### New and Experimental Features

* Enabled PCIe event counters

### Performance Improvements

* Enabled Quad DDR SPI mode to speed up loading cmfw

### Stability Improvements

* Aligned ASIC location definition in the SPI table with that of the telemetry table
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

[comment]: <> (H1 Security vulnerabilities fixed?)

## API Changes

[comment]: <> (H3 Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

[comment]: <> (H2 New Samples, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)

### Drivers

* add `maxim,max6639` driver + tests

[comment]: <> (UL Libraries)

[comment]: <> (H2 Other Notable Changes, if applicable)

[comment]: <> (UL PCIe)
[comment]: <> (UL DDR)
[comment]: <> (UL Ethernet)
[comment]: <> (UL Telemetry)
[comment]: <> (UL Debug / Developer Features)
[comment]: <> (UL Drivers)
[comment]: <> (UL Libraries)

[comment]: <> (H2 New Boards, if applicable)

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v18.5.0 release can be found in [v18.6.0 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-18.6.md).

## Full ChangeLog

The full ChangeLog from the previous v18.5.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v18.5.0...v18.6.0
