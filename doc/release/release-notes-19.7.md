# v19.7.0

> This is a working draft for the up-coming 19.7.0 release.

We are pleased to announce the release of TT Zephyr Platforms firmware version 19.7.0 🥳🎉.

Major enhancements with this release include:

## Blackhole

### Developer Features
- DMFW and SMFW now store Zephyr's `APP_VERSION_STRING` and `APP_BUILD_VERSION` binary descriptors, which provide the app version and the FW bundle's git revision respectively. These are printed to the boot banner, and can also be accessed using Zephyr's [`west bindesc` tool](https://docs.zephyrproject.org/latest/services/binary_descriptors/index.html).

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

- Updated Blackhole ERISC FW to v1.9.0
  - Embedded erisc fw version at known location (offset 0x188) for SYSIP-190
  - Increased ERISC_CODE region by 4K, taken from ERISC_DATA region
  - Updated chip_info_exchange to be more of a handshake
  - Fixed send telemetry logic bug
  - Fixed bug where is eth_sel unset the core would still attempt SerDes init
  - Reworked SerDes synchronization between 2 ETH cores sharing a SerDes
  - Added per-lane SerDes reset control
    - Reworked port action up/down to properly bring link down
    - Reinit from reset only toggles lane resets instead of full SerDes reset

<!-- Security vulnerabilities fixed? -->
<!-- API Changes, if applicable -->
<!-- Removed APIs, H3 Deprecated APIs, H3 New APIs, if applicable -->
<!-- New Samples, if applicable -->
<!-- Other Notable Changes, if applicable -->
<!-- New Boards, if applicable -->

## Migration guide

An overview of required and recommended changes to make when migrating from the previous v19.6.0 release can be found in [19.7 Migration Guide](https://github.com/tenstorrent/tt-zephyr-platforms/tree/main/doc/release/migration-guide-19.7.md).

## Full ChangeLog

The full ChangeLog from the previous v19.6.0 release can be found at the link below.

https://github.com/tenstorrent/tt-zephyr-platforms/compare/v19.6.0...v19.7.0
